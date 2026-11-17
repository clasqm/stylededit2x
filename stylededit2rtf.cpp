#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <string>

#include <Application.h>
#include <File.h>
#include <Node.h>
#include <NodeInfo.h>
#include <String.h>
#include <TextView.h>
#include <fs_attr.h>

// Structural helper to keep track of seen fonts and colors for the RTF header
struct FontInfo {
    std::string family;
    int rtfIndex;
};

struct ColorInfo {
    rgb_color color;
    int rtfIndex;
};

// Helper to compare rgb_color for map usage
struct ColorCompare {
    bool operator()(const rgb_color& a, const rgb_color& b) const {
        if (a.red != b.red) return a.red < b.red;
        if (a.green != b.green) return a.green < b.green;
        if (a.blue != b.blue) return a.blue < b.blue;
        return a.alpha < b.alpha;
    }
};

void print_usage(const char* progName) {
    printf("Usage: %s <input_stylededit_file> <output_rtf_file>\n", progName);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    // A BApplication instance is required for certain Interface Kit server interactions
    BApplication app("application/x-vnd.stylededit-rtf-converter");

    const char* inputPath = argv[1];
    const char* outputPath = argv[2];

    // 1. Open the Input File and Read Raw Text
    BFile inputFile(inputPath, B_READ_ONLY);
    if (inputFile.InitCheck() != B_OK) {
        fprintf(stderr, "Error: Cannot open input file '%s'\n", inputPath);
        return 1;
    }

    off_t textSize = 0;
    inputFile.GetSize(&textSize);
    
    std::vector<char> textBuffer(textSize + 1, 0);
    if (inputFile.Read(textBuffer.data(), textSize) != textSize) {
        fprintf(stderr, "Error: Failed to read text content from input file.\n");
        return 1;
    }
    std::string textStr(textBuffer.data(), textSize);

    // 2. Read the "styles" Extended Attribute
    attr_info attrInfo;
    text_run_array* runArray = NULL;

    if (inputFile.GetAttrInfo("styles", &attrInfo) == B_OK) {
        std::vector<uint8> attrBuffer(attrInfo.size);
        if (inputFile.ReadAttr("styles", B_RAW_TYPE, 0, attrBuffer.data(), attrInfo.size) == attrInfo.size) {
            int32 numBytes = attrInfo.size;
            // Decode the BeOS/Haiku flattened style run structure
            runArray = BTextView::UnflattenRunArray(attrBuffer.data(), &numBytes);
        }
    }

    // 3. Open Output RTF File
    FILE* rtfFile = fopen(outputPath, "w");
    if (!rtfFile) {
        fprintf(stderr, "Error: Cannot open output file '%s' for writing\n", outputPath);
        if (runArray) free(runArray);
        return 1;
    }

    // 4. Fallback if the file has no styles (treat as uniform regular plain text)
    text_run_array fallbackArray;
    if (runArray == NULL) {
        fallbackArray.count = 1;
        fallbackArray.runs[0].offset = 0;
        fallbackArray.runs[0].font = *be_plain_font;
        fallbackArray.runs[0].color = {0, 0, 0, 255}; // Default Black
        runArray = &fallbackArray;
    }

    // 5. Index Unique Fonts and Colors to build the RTF Header Tables (\fonttbl & \colortbl)
    std::map<std::string, int> fontMap;
    std::vector<std::string> fontList;
    
    std::map<rgb_color, int, ColorCompare> colorMap;
    std::vector<rgb_color> colorList;

    for (int32 i = 0; i < runArray->count; i++) {
        font_family family;
        font_style style;
        runArray->runs[i].font.GetFamilyAndStyle(&family, &style);
        std::string familyStr(family);

        if (fontMap.find(familyStr) == fontMap.end()) {
            fontMap[familyStr] = fontList.size();
            fontList.push_back(familyStr);
        }

        rgb_color color = runArray->runs[i].color;
        if (colorMap.find(color) == colorMap.end()) {
            colorMap[color] = colorList.size() + 1; // RTF colors are 1-indexed
            colorList.push_back(color);
        }
    }

    // Write RTF Prologue
    fprintf(rtfFile, "{\\rtf1\\ansi\\ansicpg1252\\deff0\n");

    // Write Font Table
    fprintf(rtfFile, "{\\fonttbl");
    for (size_t i = 0; i < fontList.size(); i++) {
        fprintf(rtfFile, "{\\f%zu\\fnil\\fcharset0 %s;}", i, fontList[i].c_str());
    }
    fprintf(rtfFile, "}\n");

    // Write Color Table
    fprintf(rtfFile, "{\\colortbl ;");
    for (size_t i = 0; i < colorList.size(); i++) {
        fprintf(rtfFile, "\\red%d\\green%d\\blue%d;", colorList[i].red, colorList[i].green, colorList[i].blue);
    }
    fprintf(rtfFile, "}\n");

    // 6. Parse and Emit Styled String Runs
    for (int32 i = 0; i < runArray->count; i++) {
        int32 startOffset = runArray->runs[i].offset;
        int32 endOffset = (i + 1 < runArray->count) ? runArray->runs[i + 1].offset : textStr.size();

        if (startOffset >= (int32)textStr.size()) break;
        if (endOffset > (int32)textStr.size()) endOffset = textStr.size();
        if (startOffset >= endOffset) continue;

        BFont font = runArray->runs[i].font;
        font_family family;
        font_style style;
        font.GetFamilyAndStyle(&family, &style);
        
        int fontIdx = fontMap[std::string(family)];
        int colorIdx = colorMap[runArray->runs[i].color];
        
        // RTF Font sizes are measured in half-points (e.g., 12pt font is \fs24)
        int fontSizeHalfPoints = (int)(font.Size() * 2.0f);
        uint16 faceFlags = font.Face();

        // Emit format changes for this run
        fprintf(rtfFile, "{\\f%d\\fs%d\\cf%d", fontIdx, fontSizeHalfPoints, colorIdx);
        if (faceFlags & B_BOLD_FACE)      fprintf(rtfFile, "\\b");
        if (faceFlags & B_ITALIC_FACE)    fprintf(rtfFile, "\\i");
        if (faceFlags & B_UNDERSCORE_FACE) fprintf(rtfFile, "\\ul");
        if (faceFlags & B_STRIKEOUT_FACE) fprintf(rtfFile, "\\strike");
        
        fprintf(rtfFile, " "); // Space character terminates the control string formatting block

        // Write content characters safely handling RTF control escaping
        for (int32 j = startOffset; j < endOffset; j++) {
            unsigned char c = textStr[j];
            if (c == '\\' || c == '{' || c == '}') {
                fprintf(rtfFile, "\\%c", c);
            } else if (c == '\n') {
                fprintf(rtfFile, "\\line\n");
            } else if (c == '\t') {
                fprintf(rtfFile, "\\tab ");
            } else if (c > 127) {
                // Escape UTF-8/Unicode sequences into RTF control bytes
                fprintf(rtfFile, "\\'%02x", c);
            } else {
                fputc(c, rtfFile);
            }
        }
        fprintf(rtfFile, "}"); // Close style block group scope
    }

    fprintf(rtfFile, "\n}\n"); // Close root RTF group scope
    fclose(rtfFile);

    if (runArray != &fallbackArray) {
        free(runArray);
    }

    printf("Successfully converted '%s' to RTF format at '%s'.\n", inputPath, outputPath);
    return 0;
}
