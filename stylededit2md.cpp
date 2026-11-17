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
    printf("Usage: %s <input_stylededit_file> <output_markdown_file>\n", progName);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    BApplication app("application/x-vnd.stylededit-markdown-converter");

    const char* inputPath = argv[1];
    const char* outputPath = argv[2];

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

    attr_info attrInfo;
    text_run_array* runArray = NULL;

    if (inputFile.GetAttrInfo("styles", &attrInfo) == B_OK) {
        std::vector<uint8> attrBuffer(attrInfo.size);
        if (inputFile.ReadAttr("styles", B_RAW_TYPE, 0, attrBuffer.data(), attrInfo.size) == attrInfo.size) {
            int32 numBytes = attrInfo.size;
            runArray = BTextView::UnflattenRunArray(attrBuffer.data(), &numBytes);
        }
    }

    FILE* mdFile = fopen(outputPath, "w");
    if (!mdFile) {
        fprintf(stderr, "Error: Cannot open output file '%s' for writing\n", outputPath);
        if (runArray) free(runArray);
        return 1;
    }

    text_run_array fallbackArray;
    if (runArray == NULL) {
        fallbackArray.count = 1;
        fallbackArray.runs[0].offset = 0;
        fallbackArray.runs[0].font = *be_plain_font;
        fallbackArray.runs[0].color = {0, 0, 0, 255};
        runArray = &fallbackArray;
    }

    for (int32 i = 0; i < runArray->count; i++) {
        int32 startOffset = runArray->runs[i].offset;
        int32 endOffset = (i + 1 < runArray->count) ? runArray->runs[i + 1].offset : textStr.size();

        if (startOffset >= (int32)textStr.size()) break;
        if (endOffset > (int32)textStr.size()) endOffset = textStr.size();
        if (startOffset >= endOffset) continue;

        BFont font = runArray->runs[i].font;
        uint16 faceFlags = font.Face();

        std::string runContent = textStr.substr(startOffset, endOffset - startOffset);

        std::string prefix = "";
        std::string suffix = "";

        // Underline uses HTML <u> tags because Markdown has no native equivalent
        if (faceFlags & B_UNDERSCORE_FACE) {
            prefix += "<u>";
            suffix = "</u>" + suffix;
        }
        if (faceFlags & B_BOLD_FACE) {
            prefix += "**";
            suffix = "**" + suffix;
        }
        if (faceFlags & B_ITALIC_FACE) {
            prefix += "*";
            suffix = "*" + suffix;
        }
        if (faceFlags & B_STRIKEOUT_FACE) {
            prefix += "~~";
            suffix = "~~" + suffix;
        }

        fputs(prefix.c_str(), mdFile);
        fputs(runContent.c_str(), mdFile);
        fputs(suffix.c_str(), mdFile);
    }

    fclose(mdFile);

    if (runArray != &fallbackArray) {
        free(runArray);
    }

    printf("Successfully converted '%s' to Markdown (with HTML underlines) at '%s'.\n", inputPath, outputPath);
    return 0;
}
