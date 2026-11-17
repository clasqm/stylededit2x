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

// Reuse CRC32 matching framework
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

        for (size_t i = 0; i < entries.size(); i++) {
            entries[i].offset = ftell(f);
            uint32_t sig = 0x04034b50;
            uint16_t version = 10;
            uint16_t flags = 0;
            uint16_t compression = 0; 
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
        printf("Usage: %s <input_stylededit_file> <output_odt_file>\n", argv[0]);
        return 1;
    }

    BApplication app("application/x-vnd.stylededit-odt-converter");

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

    // ODT splits the style parameters from structural elements
    std::string styleBlocks = "";
    std::string bodyContent = "";

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

        std::string styleName = "T" + std::to_string(i);

        // Generate ODT automatic styling descriptor block
        styleBlocks += "    <style:style style:name=\"" + styleName + "\" style:family=\"text\">\n";
        styleBlocks += "      <style:text-properties fo:font-family=\"" + std::string(family) + "\"";
        styleBlocks += " fo:font-size=\"" + std::to_string((int)font.Size()) + "pt\"";
        
        char hexColor[8];
        snprintf(hexColor, sizeof(hexColor), "#%02x%02x%02x", color.red, color.green, color.blue);
        styleBlocks += " fo:color=\"" + std::string(hexColor) + "\"";

        if (faceFlags & B_BOLD_FACE)       styleBlocks += " fo:font-weight=\"bold\"";
        if (faceFlags & B_ITALIC_FACE)     styleBlocks += " fo:font-style=\"italic\"";
        if (faceFlags & B_UNDERSCORE_FACE) styleBlocks += " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"";
        if (faceFlags & B_STRIKEOUT_FACE)  styleBlocks += " style:text-line-through-style=\"solid\"";
        
        styleBlocks += "/>\n";
        styleBlocks += "    </style:style>\n";

        // Generate content span tag mapping
        bodyContent += "<text:span text:style-name=\"" + styleName + "\">";
        std::string runContent = textStr.substr(startOffset, endOffset - startOffset);
        for (size_t ch = 0; ch < runContent.size(); ch++) {
            if (runContent[ch] == '\n') {
                bodyContent += "</text:span><text:line-break/><text:span text:style-name=\"" + styleName + "\">";
            } else {
                std::string s(1, runContent[ch]);
                bodyContent += escape_xml(s);
            }
        }
        bodyContent += "</text:span>";
    }

    // Assemble the content.xml payload
    std::string contentXml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    contentXml += "<office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" office:version=\"1.2\">\n";
    contentXml += "  <office:automatic-styles>\n" + styleBlocks + "  </office:automatic-styles>\n";
    contentXml += "  <office:body>\n";
    contentXml += "    <office:text>\n";
    contentXml += "      <text:p>" + bodyContent + "</text:p>\n";
    contentXml += "    </office:text>\n";
    contentXml += "  </office:body>\n";
    contentXml += "</office:document-content>\n";

    SimpleZipArchive zip;
    // ODT specifications explicitly demand 'mimetype' to be the first item and uncompressed
    zip.AddFile("mimetype", "application/vnd.oasis.opendocument.text");
    zip.AddFile("content.xml", contentXml);
    zip.AddFile("META-INF/manifest.xml", 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.2\">\n"
        "  <manifest:file-entry manifest:full-path=\"/\" manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>\n"
        "  <manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>\n"
        "</manifest:manifest>\n");

    if (zip.WriteArchive(outputPath)) {
        printf("Successfully converted '%s' to ODT format at '%s'.\n", inputPath, outputPath);
    } else {
        fprintf(stderr, "Error producing destination ODT bundle.\n");
    }

    if (runArray != &fallbackArray) free(runArray);
    return 0;
}