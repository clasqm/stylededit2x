#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <sstream>

#include <Application.h>
#include <File.h>
#include <fs_attr.h>
#include <Node.h>
#include <NodeInfo.h>
#include <TextView.h>

void print_usage(const char* progName) {
    printf("Usage: %s <input_stylededit_file> <output_pdf_file>\n", progName);
}

// Escapes special PDF string control characters: opening parenthesis, closing parenthesis, and backslash
std::string escape_pdf_string(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        char c = input[i];
        if (c == '(' || c == ')' || c == '\\') {
            output += '\\';
        }
        output += c;
    }
    return output;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    BApplication app("application/x-vnd.stylededit-pdf-converter");

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

    // 2. Read "styles" Extended Attribute
    attr_info attrInfo;
    text_run_array* runArray = NULL;

    if (inputFile.GetAttrInfo("styles", &attrInfo) == B_OK) {
        std::vector<uint8> attrBuffer(attrInfo.size);
        if (inputFile.ReadAttr("styles", B_RAW_TYPE, 0, attrBuffer.data(), attrInfo.size) == attrInfo.size) {
            int32 numBytes = attrInfo.size;
            runArray = BTextView::UnflattenRunArray(attrBuffer.data(), &numBytes);
        }
    }

    // 3. Fallback if the file has zero styles
    text_run_array fallbackArray;
    if (runArray == NULL) {
        fallbackArray.count = 1;
        fallbackArray.runs[0].offset = 0;
        fallbackArray.runs[0].font = *be_plain_font;
        fallbackArray.runs[0].color = {0, 0, 0, 255};
        runArray = &fallbackArray;
    }

    // 4. Generate Content Stream
    std::ostringstream stream;
    
    float startX = 50.0f;
    float currentY = 740.0f; 
    float lineHeight = 16.0f;

    stream << "BT\n"; // Begin Text block
    stream << "1 0 0 1 " << startX << " " << currentY << " Tm\n"; // Position initial cursor

    for (int32 i = 0; i < runArray->count; i++) {
        int32 startOffset = runArray->runs[i].offset;
        int32 endOffset = (i + 1 < runArray->count) ? runArray->runs[i + 1].offset : textStr.size();

        if (startOffset >= (int32)textStr.size()) break;
        if (endOffset > (int32)textStr.size()) endOffset = textStr.size();
        if (startOffset >= endOffset) continue;

        BFont font = runArray->runs[i].font;
        rgb_color color = runArray->runs[i].color;
        uint16 faceFlags = font.Face();

        // Standard PDF Type1 Font mapping
        std::string pdfFont = "/F1"; // Default Helvetica (Regular)
        if ((faceFlags & B_BOLD_FACE) && (faceFlags & B_ITALIC_FACE)) {
            pdfFont = "/F4"; // BoldOblique
        } else if (faceFlags & B_BOLD_FACE) {
            pdfFont = "/F2"; // Bold
        } else if (faceFlags & B_ITALIC_FACE) {
            pdfFont = "/F3"; // Oblique
        }

        float fontSize = font.Size();
        lineHeight = fontSize * 1.25f;

        float r = color.red / 255.0f;
        float g = color.green / 255.0f;
        float b = color.blue / 255.0f;

        // Set font face, size, and RGB text fill color
        stream << pdfFont << " " << fontSize << " Tf\n";
        stream << r << " " << g << " " << b << " rg\n";

        std::string runContent = textStr.substr(startOffset, endOffset - startOffset);
        std::string currentSegment = "";

        for (size_t ch = 0; ch < runContent.size(); ch++) {
            char c = runContent[ch];
            if (c == '\n') {
                if (!currentSegment.empty()) {
                    stream << "(" << escape_pdf_string(currentSegment) << ") Tj\n";
                    currentSegment.clear();
                }
                // Move text positioning matrix down for new lines
                stream << "0 -" << lineHeight << " Td\n";
            } else {
                currentSegment += c;
            }
        }

        if (!currentSegment.empty()) {
            stream << "(" << escape_pdf_string(currentSegment) << ") Tj\n";
        }
    }

    stream << "ET\n"; // End Text block

    std::string streamData = stream.str();

    // 5. Assemble PDF Catalog Structure & Object Offsets
    FILE* pdfFile = fopen(outputPath, "w");
    if (!pdfFile) {
        fprintf(stderr, "Error: Cannot open output file '%s' for writing\n", outputPath);
        if (runArray != &fallbackArray) free(runArray);
        return 1;
    }

    std::vector<long> offsets;
    
    // Header
    fprintf(pdfFile, "%%PDF-1.4\n");

    // Object 1: Catalog
    offsets.push_back(ftell(pdfFile));
    fprintf(pdfFile, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    // Object 2: Page Tree
    offsets.push_back(ftell(pdfFile));
    fprintf(pdfFile, "2 0 obj\n<< /Type /Pages /Kinds [3 0 R] /Count 1 /Kids [3 0 R] >>\nendobj\n");

    // Object 3: Page Definition
    offsets.push_back(ftell(pdfFile));
    fprintf(pdfFile, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources 4 0 R /Contents 5 0 R >>\nendobj\n");

    // Object 4: Font Dictionary
    offsets.push_back(ftell(pdfFile));
    fprintf(pdfFile, "4 0 obj\n<< /Font <<\n");
    fprintf(pdfFile, "  /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n");
    fprintf(pdfFile, "  /F2 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\n");
    fprintf(pdfFile, "  /F3 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique >>\n");
    fprintf(pdfFile, "  /F4 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica-BoldOblique >>\n");
    fprintf(pdfFile, ">> >>\nendobj\n");

    // Object 5: Content Stream
    offsets.push_back(ftell(pdfFile));
    fprintf(pdfFile, "5 0 obj\n<< /Length %zu >>\nstream\n%sendstream\nendobj\n", 
            streamData.size(), streamData.c_str());

    // Write Cross-Reference Table (xref)
    long xrefOffset = ftell(pdfFile);
    fprintf(pdfFile, "xref\n0 6\n");
    fprintf(pdfFile, "0000000000 65535 f \n");
    for (long offset : offsets) {
        fprintf(pdfFile, "%010ld 00000 n \n", offset);
    }

    // Write Trailer
    fprintf(pdfFile, "trailer\n<< /Size 6 /Root 1 0 R >>\n");
    fprintf(pdfFile, "startxref\n%ld\n%%%%EOF\n", xrefOffset);

    fclose(pdfFile);

    if (runArray != &fallbackArray) {
        free(runArray);
    }

    printf("Successfully converted '%s' to PDF format at '%s'.\n", inputPath, outputPath);
    return 0;
}