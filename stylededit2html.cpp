#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>

#include <Application.h>
#include <File.h>
#include <Node.h>
#include <NodeInfo.h>
#include <TextView.h>
#include <fs_attr.h>

void print_usage(const char* progName) {
    printf("Usage: %s <input_stylededit_file> <output_html_file>\n", progName);
}

// Escapes critical markup characters to avoid broken HTML elements
void escape_html_characters(unsigned char c, FILE* htmlFile) {
    switch (c) {
        case '&':  fputs("&amp;", htmlFile);  break;
        case '<':  fputs("&lt;", htmlFile);   break;
        case '>':  fputs("&gt;", htmlFile);   break;
        case '"':  fputs("&quot;", htmlFile); break;
        case '\'': fputs("&#39;", htmlFile);  break;
        case '\n': fputs("<br>\n", htmlFile); break;
        case '\t': fputs("&nbsp;&nbsp;&nbsp;&nbsp;", htmlFile); break;
        default:   fputc(c, htmlFile);        break;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    // Initialize BApplication for Interface Kit / Server syncing
    BApplication app("application/x-vnd.stylededit-html-converter");

    const char* inputPath = argv[1];
    const char* outputPath = argv[2];

    // 1. Open File and Read Text Contents
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

    // 3. Open Output HTML File
    FILE* htmlFile = fopen(outputPath, "w");
    if (!htmlFile) {
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

    // Write HTML Shell Header
    fprintf(htmlFile, "<!DOCTYPE html>\n<html>\n<head>\n");
    fprintf(htmlFile, "  <meta charset=\"utf-8\">\n");
    fprintf(htmlFile, "  <title>Converted StyledEdit Document</title>\n");
    fprintf(htmlFile, "</head>\n<body style=\"background-color: #ffffff; color: #000000; font-family: sans-serif; padding: 20px;\">\n");
    fprintf(htmlFile, "<div class=\"styled-content\">\n");

    // 5. Parse Text Segments and Wrap inside Styled Spans
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

        // Write styles using a styled CSS inline tag
        fprintf(htmlFile, "<span style=\"font-family: '%s'; font-size: %.1fpx; color: rgb(%d, %d, %d);", 
                family, font.Size(), color.red, color.green, color.blue);
        
        // Map native weights to clean CSS declarations
        if (faceFlags & B_BOLD_FACE)      fprintf(htmlFile, " font-weight: bold;");
        if (faceFlags & B_ITALIC_FACE)    fprintf(htmlFile, " font-style: italic;");
        
        // Multi-decoration support for underlining/striking
        if ((faceFlags & B_UNDERSCORE_FACE) || (faceFlags & B_STRIKEOUT_FACE)) {
            fprintf(htmlFile, " text-decoration:");
            if (faceFlags & B_UNDERSCORE_FACE) fprintf(htmlFile, " underline");
            if (faceFlags & B_STRIKEOUT_FACE) fprintf(htmlFile, " line-through");
            fprintf(htmlFile, ";");
        }
        
        fprintf(htmlFile, "\">");

        // Loop through characters of the specific run block, printing HTML-safe text
        for (int32 j = startOffset; j < endOffset; j++) {
            escape_html_characters(textStr[j], htmlFile);
        }

        fprintf(htmlFile, "</span>");
    }

    // Close out HTML document tags
    fprintf(htmlFile, "\n</div>\n</body>\n</html>\n");
    fclose(htmlFile);

    if (runArray != &fallbackArray) {
        free(runArray);
    }

    printf("Successfully converted '%s' to HTML format at '%s'.\n", inputPath, outputPath);
    return 0;
}
