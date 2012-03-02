/* -*- mode: C; c-basic-offset: 4; intent-tabs-mode: nil -*-
 *
 * STIR -- Sifteo Tiled Image Reducer
 * M. Elizabeth Scott <beth@sifteo.com>
 *
 * Copyright <c> 2011 Sifteo, Inc. All rights reserved.
 */

#include "cppwriter.h"
#include "audioencoder.h"
#include <assert.h>

namespace Stir {

const char *CPPWriter::indent = "    ";

CPPWriter::CPPWriter(Logger &log, const char *filename)
    : mLog(log)
{
    if (filename) {
        mStream.open(filename);
        if (!mStream.is_open())
            log.error("Error opening output file '%s'", filename);
    }
    
    if (mStream.is_open())
        head();
}

void CPPWriter::head()
{
    mStream <<
        "/*\n"
        " * Generated by STIR. Do not edit by hand.\n"
        " */\n"
        "\n"
        "#include <sifteo/asset.h>\n";
}

void CPPWriter::foot()
{}
 
void CPPWriter::close()
{
    if (mStream.is_open()) {
        foot();
        mStream.close();
    }
}

void CPPWriter::writeString(const std::vector<uint8_t> &data)
{
    // Write a large uint8_t array, as a string literal. This is handled
    // more efficiently by the compiler than a large array, so the resulting
    // code compiles quickly. The downside: resulting code is very ugly!

    unsigned i = 0;
    
    while (i < data.size()) {
        unsigned lineLength = 2;
        mStream << "\"";

        while (i < data.size() && lineLength < 120) {
            uint8_t byte = data[i++];

            if (byte >= ' ' && byte <= '~' && byte != '"' && byte != '\\' && byte != '?') {
                mStream << (char)byte;
                lineLength++;
            } else {
                mStream << '\\'
                    << (char)('0' + (byte >> 6))
                    << (char)('0' + ((byte >> 3) & 7))
                    << (char)('0' + (byte & 7));
                lineLength += 4;
            }
        }

        mStream << "\"\n";
    }
}

void CPPWriter::writeArray(const std::vector<uint8_t> &data)
{
    char buf[8];

    mStream << indent;

    for (unsigned i = 0; i < data.size(); i++) {
        if (i && !(i % 16))
            mStream << "\n" << indent;

        sprintf(buf, "0x%02x,", data[i]);
        mStream << buf;
    }

    mStream << "\n";
}


CPPSourceWriter::CPPSourceWriter(Logger &log, const char *filename)
    : CPPWriter(log, filename) {}

void CPPSourceWriter::writeGroup(const Group &group)
{
    char signature[32];

#ifdef __MINGW32__
    sprintf(signature, "0x%016I64x", (long long unsigned int) group.getSignature());
#else
    sprintf(signature, "0x%016llx", (long long unsigned int) group.getSignature());
#endif

    mStream <<
        "\n"
        "static const struct {\n" <<
        indent << "struct _SYSAssetGroupHeader hdr;\n" <<
        indent << "uint8_t data[" << group.getLoadstream().size() << "];\n"
        "} " << group.getName() << "_data = {{\n" <<
        indent << "/* hdrSize   */ sizeof(struct _SYSAssetGroupHeader),\n" <<
        indent << "/* reserved  */ 0,\n" <<
        indent << "/* numTiles  */ " << group.getPool().size() << ",\n" <<
        indent << "/* dataSize  */ " << group.getLoadstream().size() << ",\n" <<
        indent << "/* signature */ " << signature << ",\n"
        "}, {\n";

    writeArray(group.getLoadstream());

    mStream <<
        "}};\n\n"
        "Sifteo::AssetGroup " << group.getName() <<
        " = {{ &" << group.getName() << "_data.hdr, " << group.getName() << ".cubes }};\n";

    for (std::set<Image*>::iterator i = group.getImages().begin();
         i != group.getImages().end(); i++)
        writeImage(**i);
}

void CPPSourceWriter::writeSound(const Sound &sound)
{
    std::vector<uint8_t> data;
    AudioEncoder *enc = AudioEncoder::create(sound.getEncode(), sound.getQuality());
    assert(enc != 0);

    float kbps;
    enc->encodeFile(sound.getFile(), data, kbps);
    mLog.infoLine("%20s: %7.02f kiB, %6.02f kbps %s (%s)", sound.getName().c_str(),
        data.size() / 1024.0f, kbps, enc->getName(), sound.getFile().c_str());

    if (data.empty())
        mLog.error("Error encoding audio file '%s'", sound.getFile().c_str());

    mStream << "static const char " << sound.getName() << "_data[] = \n";
    writeString(data);
    mStream << ";\n\n";

    mStream <<
        "extern const Sifteo::AssetAudio " << sound.getName() << " = {{\n" <<
        indent << "/* type      */ " << enc->getTypeSymbol() << ",\n" <<
        indent << "/* reserved0 */ " << 0 << ",\n" <<
        indent << "/* reserved1 */ " << 0 << ",\n" <<
        indent << "/* dataSize  */ " << data.size() << ",\n" <<
        indent << "/* data      */ (const uint8_t *) " << sound.getName() << "_data\n" <<
        "}};\n\n";

    delete enc;
}

void CPPSourceWriter::writeImage(const Image &image)
{
    char buf[16];
    const std::vector<TileGrid> &grids = image.getGrids();
    unsigned width = grids.empty() ? 0 : grids[0].width();
    unsigned height = grids.empty() ? 0 : grids[0].height();

    if (image.isPinned()) {
        /*
         * Sifteo::PinnedAssetImage
         */
        
        const TileGrid &grid = grids[0];
        const TilePool &pool = grid.getPool();

        mStream <<
            "\n"
            "extern const Sifteo::PinnedAssetImage " << image.getName() << " = {\n" <<
            indent << "/* width   */ " << width << ",\n" <<
            indent << "/* height  */ " << height << ",\n" <<
            indent << "/* frames  */ " << grids.size() << ",\n" <<
            indent << "/* index   */ " << pool.index(grid.tile(0, 0)) <<
            ",\n};\n\n";
            
    } else {
        /*
         * Sifteo::AssetImage
         *
         * Write out an uncompressed tile grid.
         *
         * XXX: Compression!
         */

        mStream << "\nstatic const uint16_t " << image.getName() << "_tiles[] = {\n";

        for (unsigned f = 0; f < grids.size(); f++) {
            const TileGrid &grid = grids[f];
            const TilePool &pool = grid.getPool();

            mStream << indent << "// Frame " << f << "\n";
        
            for (unsigned y = 0; y < height; y++) {
                mStream << indent;
                for (unsigned x = 0; x < width; x++) {
                    sprintf(buf, "0x%04x,", pool.index(grid.tile(x, y)));
                    mStream << buf;
                }
                mStream << "\n";
            }
        }
        
        mStream <<
            "};\n\n"
            "extern const Sifteo::AssetImage " << image.getName() << " = {\n" <<
            indent << "/* width   */ " << width << ",\n" <<
            indent << "/* height  */ " << height << ",\n" <<
            indent << "/* frames  */ " << grids.size() << ",\n" <<
            indent << "/* tiles   */ " << image.getName() << "_tiles,\n" <<
            "};\n\n";
    }
}

CPPHeaderWriter::CPPHeaderWriter(Logger &log, const char *filename)
    : CPPWriter(log, filename)
{
    if (filename)
        createGuardName(filename);

    if (mStream.is_open())
        head();
}

void CPPHeaderWriter::createGuardName(const char *filename)
{
    /*
     * Make a name for the include guard, based on the filename
     */

    char c;
    char prev = '_';
    guardName = prev;

    while ((c = *filename)) {
        c = toupper(c);

        if (isalpha(c)) {
            prev = c;
            guardName += prev;
        } else if (prev != '_') {
            prev = '_';
            guardName += prev;
        }

        filename++;
    }
}

void CPPHeaderWriter::head()
{
    mStream <<
        "\n"
        "#ifndef " << guardName << "\n"
        "#define " << guardName << "\n"
        "\n";
}

void CPPHeaderWriter::foot()
{
    mStream <<
        "\n"
        "#endif  // " << guardName << "\n";

    CPPWriter::foot();
}

void CPPHeaderWriter::writeGroup(const Group &group)
{
    mStream << "extern Sifteo::AssetGroup " << group.getName() << ";\n";

    for (std::set<Image*>::iterator i = group.getImages().begin();
         i != group.getImages().end(); i++) {
        Image *image = *i;
        const char *cls;
        
        if (image->isPinned())
            cls = "PinnedAssetImage";
        else
            cls = "AssetImage";
            
        mStream << "extern const Sifteo::" << cls << " " << image->getName() << ";\n";
    }
}

void CPPHeaderWriter::writeSound(const Sound &sound)
{
    mStream << "extern const Sifteo::AssetAudio " << sound.getName() << ";\n";
}

};  // namespace Stir
