#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>

#include <Application.h>
#include <File.h>
#include <fs_attr.h>
#include <Node.h>
#include <NodeInfo.h>
#include <TextView.h>

void print_usage(const char* progName) {
    printf("Usage: %s <input_stylededit_file> <output_abw_file>\n", progName);
}

// Escapes special characters for XML safety
std::string escape_xml(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        char c = input[i];
        switch (c) {
            case '&':  output += "&amp;";  break;
            case '<':  output += "&lt;";   break;
            case '>':  output += "&gt;";   break;
            case '"':  output += "&quot;"; break;
            case '\'': output += "&apos;"; break;
            default:   output += c;        break;
        }
    }
    return output;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    // BApplication required for Interface Kit / Server syncing
    BApplication app("application/x-vnd.stylededit-abiword-converter");

    const char* inputPath = argv[1];
    const char* outputPath = argv[2];

    // 1. Open Input File and Read Raw Text
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
            runArray = BTextView::UnflattenRunArray(attrBuffer.data(), &numBytes);
        }
    }

    // 3. Open Output File (fopen with "w" overwrites automatically)
    FILE* abwFile = fopen(outputPath, "w");
    if (!abwFile) {
        fprintf(stderr, "Error: Cannot open output file '%s' for writing\n", outputPath);
        if (runArray) free(runArray);
        return 1;
    }

    // 4. Fallback if the file has zero styles
    text_run_array fallbackArray;
    if (runArray == NULL) {
        fallbackArray.count = 1;
        fallbackArray.runs[0].offset = 0;
        fallbackArray.runs[0].font = *be_plain_font;
        fallbackArray.runs[0].color = {0, 0, 0, 255};
        runArray = &fallbackArray;
    }

    // 5. Write AbiWord File Header
    fprintf(abwFile, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(abwFile, "<!DOCTYPE abiword PUBLIC \"-//ABIWORD//DTD ABIWORD Strict Version 1.0//EN\" \"http://www.abisource.com/abiword_strict_v10.dtd\">\n");
    fprintf(abwFile, "<abiword template=\"false\" xmlns:fo=\"http://www.w3.org/1999/XSL/Format\" xmlns:svg=\"http://www.w3.org/2000/svg\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" version=\"2.0\" xml:space=\"preserve\">\n");
    fprintf(abwFile, "<styles>\n");
    fprintf(abwFile, "  <style name=\"Normal\" type=\"paragraph\" props=\"font-family:Times New Roman; font-size:12pt; color:000000\"/>\n");
    fprintf(abwFile, "</styles>\n");
    fprintf(abwFile, "<section id=\"0\">\n");
    fprintf(abwFile, "<p style=\"Normal\">\n");

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
        
        rgb_color color = runArray->runs[i].color;
        uint16 faceFlags = font.Face();

        // Build inline style props for <c> tag
        char hexColor[7];
        snprintf(hexColor, sizeof(hexColor), "%02x%02x%02x", color.red, color.green, color.blue);

        std::string props = "font-family:" + std::string(family) + "; ";
        props += "font-size:" + std::to_string((int)font.Size()) + "pt; ";
        props += "color:" + std::string(hexColor);

        if (faceFlags & B_BOLD_FACE)       props += "; font-weight:bold";
        if (faceFlags & B_ITALIC_FACE)     props += "; font-style:italic";
        if (faceFlags & B_UNDERSCORE_FACE) props += "; text-decoration:underline";
        if (faceFlags & B_STRIKEOUT_FACE)  props += "; text-decoration:line-through";

        fprintf(abwFile, "<c props=\"%s\">", props.c_str());

        // Process text and convert newlines to AbiWord line breaks
        std::string runContent = textStr.substr(startOffset, endOffset - startOffset);
        for (size_t ch = 0; ch < runContent.size(); ch++) {
            if (runContent[ch] == '\n') {
                fprintf(abwFile, "</c><br/><c props=\"%s\">", props.c_str());
            } else {
                std::string s(1, runContent[ch]);
                fputs(escape_xml(s).c_str(), abwFile);
            }
        }

        fprintf(abwFile, "</c>");
    }

    // 7. Close Document Tags
    fprintf(abwFile, "\n</p>\n");
    fprintf(abwFile, "</section>\n");
    fprintf(abwFile, "</abiword>\n");

    fclose(abwFile);

    if (runArray != &fallbackArray) {
        free(runArray);
    }

    printf("Successfully converted '%s' to AbiWord (.abw) format at '%s'.\n", inputPath, outputPath);
    return 0;
}