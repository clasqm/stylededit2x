#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector>
#include <string>

#include <Application.h>
#include <File.h>
#include <fs_attr.h>
#include <Node.h>
#include <NodeInfo.h>
#include <TextView.h>

// --- Lightweight Custom ZIP Archiver ---
uint32_t calculate_crc32(const unsigned char *buf, size_t len) {
    static uint32_t table[256];
    static int have_table = 0;
    if (!have_table) {
        for (int i = 0; i < 256; i++) {
            uint32_t rem = i;
            for (int j = 0; j < 8; j++) {
                if (rem & 1) rem = (rem >> 1) ^ 0xedb88320UL;
                else rem >>= 1;
            }
            table[i] = rem;
        }
        have_table = 1;
    }
    uint32_t crc = ~0;
    while (len--) {
        crc = table[(crc ^ *buf++) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

struct ZipFileEntry {
    std::string name;
    std::string data;
    uint32_t crc;
    uint32_t offset;
};

class SimpleZipArchive {
public:
    std::vector<ZipFileEntry> entries;

    void AddFile(const std::string& name, const std::string& data) {
        ZipFileEntry entry;
        entry.name = name;
        entry.data = data;
        entry.crc = calculate_crc32((const unsigned char*)data.data(), data.size());
        entry.offset = 0;
        entries.push_back(entry);
    }

    bool WriteArchive(const char* path) {
        FILE* f = fopen(path, "wb");
        if (!f) return false;

        // 1. Write Local File Headers and Data Blocks
        for (size_t i = 0; i < entries.size(); i++) {
            entries[i].offset = ftell(f);
            uint32_t sig = 0x04034b50;
            uint16_t version = 10;
            uint16_t flags = 0;
            uint16_t compression = 0; // Stored (Uncompressed)
            uint16_t modTime = 0;
            uint16_t modDate = 0;
            uint32_t crc = entries[i].crc;
            uint32_t compSize = entries[i].data.size();
            uint32_t uncompSize = entries[i].data.size();
            uint16_t nameLen = entries[i].name.size();
            uint16_t extraLen = 0;

            fwrite(&sig, 4, 1, f);
            fwrite(&version, 2, 1, f);
            fwrite(&flags, 2, 1, f);
            fwrite(&compression, 2, 1, f);
            fwrite(&modTime, 2, 1, f);
            fwrite(&modDate, 2, 1, f);
            fwrite(&crc, 4, 1, f);
            fwrite(&compSize, 4, 1, f);
            fwrite(&uncompSize, 4, 1, f);
            fwrite(&nameLen, 2, 1, f);
            fwrite(&extraLen, 2, 1, f);
            fwrite(entries[i].name.data(), 1, nameLen, f);
            fwrite(entries[i].data.data(), 1, compSize, f);
        }

        // 2. Write Central Directory Headers
        uint32_t centralDirOffset = ftell(f);
        for (size_t i = 0; i < entries.size(); i++) {
            uint32_t sig = 0x02014b50;
            uint16_t versionMade = 20;
            uint16_t versionNeed = 10;
            uint16_t flags = 0;
            uint16_t compression = 0;
            uint16_t modTime = 0;
            uint16_t modDate = 0;
            uint32_t crc = entries[i].crc;
            uint32_t compSize = entries[i].data.size();
            uint32_t uncompSize = entries[i].data.size();
            uint16_t nameLen = entries[i].name.size();
            uint16_t extraLen = 0;
            uint16_t commentLen = 0;
            uint16_t diskStart = 0;
            uint16_t intAttr = 0;
            uint32_t extAttr = 0;
            uint32_t localOffset = entries[i].offset;

            fwrite(&sig, 4, 1, f);
            fwrite(&versionMade, 2, 1, f);
            fwrite(&versionNeed, 2, 1, f);
            fwrite(&flags, 2, 1, f);
            fwrite(&compression, 2, 1, f);
            fwrite(&modTime, 2, 1, f);
            fwrite(&modDate, 2, 1, f);
            fwrite(&crc, 4, 1, f);
            fwrite(&compSize, 4, 1, f);
            fwrite(&uncompSize, 4, 1, f);
            fwrite(&nameLen, 2, 1, f);
            fwrite(&extraLen, 2, 1, f);
            fwrite(&commentLen, 2, 1, f);
            fwrite(&diskStart, 2, 1, f);
            fwrite(&intAttr, 2, 1, f);
            fwrite(&extAttr, 4, 1, f);
            fwrite(&localOffset, 4, 1, f);
            fwrite(entries[i].name.data(), 1, nameLen, f);
        }

        uint32_t centralDirEnd = ftell(f);
        uint32_t centralDirSize = centralDirEnd - centralDirOffset;

        // 3. End of Central Directory
        uint32_t eocdSig = 0x06054b50;
        uint16_t diskNum = 0;
        uint16_t cdDisk = 0;
        uint16_t diskEntries = entries.size();
        uint16_t totalEntries = entries.size();
        uint32_t cdSize = centralDirSize;
        uint32_t cdOffset = centralDirOffset;
        uint16_t commentLen = 0;

        fwrite(&eocdSig, 4, 1, f);
        fwrite(&diskNum, 2, 1, f);
        fwrite(&cdDisk, 2, 1, f);
        fwrite(&diskEntries, 2, 1, f);
        fwrite(&totalEntries, 2, 1, f);
        fwrite(&cdSize, 4, 1, f);
        fwrite(&cdOffset, 4, 1, f);
        fwrite(&commentLen, 2, 1, f);

        fclose(f);
        return true;
    }
};

// --- XML String Escaper ---
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
        printf("Usage: %s <input_stylededit_file> <output_docx_file>\n", argv[0]);
        return 1;
    }

    BApplication app("application/x-vnd.stylededit-docx-converter");

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
    inputFile.Read(textBuffer.data(), textSize);
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

    text_run_array fallbackArray;
    if (runArray == NULL) {
        fallbackArray.count = 1;
        fallbackArray.runs[0].offset = 0;
        fallbackArray.runs[0].font = *be_plain_font;
        fallbackArray.runs[0].color = {0, 0, 0, 255};
        runArray = &fallbackArray;
    }

    // Build the Main Word document XML payload
    std::string docXml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    docXml += "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n";
    docXml += "  <w:body>\n";
    docXml += "    <w:p>\n";

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

        docXml += "      <w:r>\n";
        docXml += "        <w:rPr>\n";
        docXml += "          <w:rFonts w:ascii=\"" + std::string(family) + "\" w:hAnsi=\"" + std::string(family) + "\"/>\n";
        
        char hexColor[7];
        snprintf(hexColor, sizeof(hexColor), "%02X%02X%02X", color.red, color.green, color.blue);
        docXml += "          <w:color w:val=\"" + std::string(hexColor) + "\"/>\n";
        
        // Size inside OpenXML is measured in half-points
        int docxSize = (int)(font.Size() * 2.0f);
        docXml += "          <w:sz w:val=\"" + std::to_string(docxSize) + "\"/>\n";

        if (faceFlags & B_BOLD_FACE)       docXml += "          <w:b/>\n";
        if (faceFlags & B_ITALIC_FACE)     docXml += "          <w:i/>\n";
        if (faceFlags & B_UNDERSCORE_FACE) docXml += "          <w:u w:val=\"single\"/>\n";
        if (faceFlags & B_STRIKEOUT_FACE)  docXml += "          <w:strike/>\n";
        docXml += "        </w:rPr>\n";

        // Write content, switching \n to <w:br/>
        docXml += "        <w:t xml:space=\"preserve\">";
        std::string runContent = textStr.substr(startOffset, endOffset - startOffset);
        for (size_t ch = 0; ch < runContent.size(); ch++) {
            if (runContent[ch] == '\n') {
                docXml += "</w:t><w:br/><w:t xml:space=\"preserve\">";
            } else {
                std::string s(1, runContent[ch]);
                docXml += escape_xml(s);
            }
        }
        docXml += "</w:t>\n";
        docXml += "      </w:r>\n";
    }

    docXml += "    </w:p>\n";
    docXml += "  </w:body>\n";
    docXml += "</w:document>\n";

    // Setup Standard OpenXML structural package mapping
    SimpleZipArchive zip;
    zip.AddFile("word/document.xml", docXml);
    zip.AddFile("[Content_Types].xml", 
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        "  <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        "  <Default Extension=\"xml\" ContentType=\"application/xml\"/>\n"
        "  <Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>\n"
        "</Types>\n");
    zip.AddFile("_rels/.rels", 
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        "  <Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>\n"
        "</Relationships>\n");

    if (zip.WriteArchive(outputPath)) {
        printf("Successfully converted '%s' to DOCX format at '%s'.\n", inputPath, outputPath);
    } else {
        fprintf(stderr, "Error producing destination DOCX bundle.\n");
    }

    if (runArray != &fallbackArray) free(runArray);
    return 0;
}