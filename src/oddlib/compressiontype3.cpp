#include "oddlib/compressiontype3.hpp"
#include "oddlib/stream.hpp"
#include "logger.hpp"
#include <windows.h>

namespace Oddlib
{
    static int Next6Bits(signed int& bitCounter, unsigned int& src_data, IStream& stream)
    {
        int ret = 0;
        if (bitCounter > 0)
        {
            if (bitCounter == 14)
            {
                bitCounter = 30;
                src_data = (ReadUint16(stream) << 14) | src_data;
                ret = 2;
            }
        }
        else
        {
            bitCounter = 32;
            src_data = ReadUint32(stream);
            ret = 4;
        }
        bitCounter -= 6;
        return ret;
    }

    // Function 0x004031E0 in AO
    std::vector<Uint8> CompressionType3::Decompress(IStream& stream, Uint32 finalW, Uint32 /*w*/, Uint32 h, Uint32 dataSize)
    {
        size_t dstPos = 0;
        std::vector<unsigned char> buffer;
        buffer.resize(finalW*h*4);

      //  const unsigned int whSize = w*h;
        int numBytesInFrameCnt = dataSize;// whSize & 0xFFFFFFFC;
        if (numBytesInFrameCnt > 0)
        {

            // unsigned int frame_data_index = 0;
            unsigned int src_data = 0;
            signed int bitCounter = 0;
            do
            {

                /*numBytesInFrameCnt -=*/ Next6Bits(bitCounter, src_data, stream);
                unsigned char bits = src_data & 0x3F;
                src_data = src_data >> 6;
                 --numBytesInFrameCnt;
                if (bits & 0x20)
                {
                    // 0x20= 0b100000
                    // 0x3F= 0b111111
                    int numberOfBytes = (bits & 0x1F) + 1;
                    if (numberOfBytes)
                    {
                        do
                        {
                            if (numBytesInFrameCnt && dstPos < buffer.size())
                            {
                                /*numBytesInFrameCnt -=*/ Next6Bits(bitCounter, src_data, stream);
                                bits = src_data & 0x3F;
                                src_data = src_data >> 6;
                                 --numBytesInFrameCnt;
                            }
                            else
                            {
                                // 6 bits from "other" source
                                bits = 0;// *srcPtr++ & 0x3F;
                                //--for_counter;
                            }
                          //  if (dstPos < buffer.size())
                            {
                                buffer[dstPos++] = bits;
                            }
                        } while (--numberOfBytes != 0);
                    }
                }
                else
                {
                    // literal flag isn't set, so we have up to 5 bits of "black" pixels
                    dstPos += bits + 1;
                }

            } while (numBytesInFrameCnt);
        }

        return buffer;
    }
}
