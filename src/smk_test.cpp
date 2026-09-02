#include <cstring>
#include <fstream>
#include <iostream>
#include <rwe/io/smk/SmkDecoder.h>
#include <string>
#include <vector>

// Dumps decoded Smacker video and audio in the raw formats FFmpeg emits,
// so the decoder can be checked against it byte for byte:
//   ffmpeg -i in.smk -f rawvideo -pix_fmt rgb24 ref.rgb
//   ffmpeg -i in.smk -f s16le ref.pcm

std::vector<char> readWholeFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("cannot open " + path);
    }
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        std::cerr << "usage: smk_test <info|dumpvideo|dumpaudio> <file.smk> [out] [maxframes]" << std::endl;
        return 1;
    }

    std::string command = argv[1];
    rwe::SmkDecoder decoder(readWholeFile(argv[2]));

    if (command == "info")
    {
        std::cout << "size: " << decoder.getWidth() << "x" << decoder.getHeight() << "\n"
                  << "frames: " << decoder.getFrameCount() << "\n"
                  << "us/frame: " << decoder.getMicrosecondsPerFrame() << "\n";
        if (auto audio = decoder.getAudioInfo())
        {
            std::cout << "audio: " << audio->sampleRate << "Hz "
                      << (audio->is16Bit ? 16 : 8) << "-bit "
                      << (audio->stereo ? "stereo" : "mono") << "\n";
        }
        return 0;
    }

    unsigned int maxFrames = decoder.getFrameCount();
    if (argc >= 5)
    {
        maxFrames = static_cast<unsigned int>(std::stoul(argv[4]));
    }

    if (command == "dumpvideo")
    {
        std::ofstream out(argv[3], std::ios::binary);
        std::vector<uint8_t> rgb(decoder.getWidth() * decoder.getHeight() * 3);
        for (unsigned int f = 0; f < maxFrames && decoder.decodeNextFrame(); ++f)
        {
            const auto& indices = decoder.getVideoIndices();
            const auto& palette = decoder.getPalette();
            for (std::size_t i = 0; i < indices.size(); ++i)
            {
                std::memcpy(&rgb[i * 3], palette[indices[i]].data(), 3);
            }
            out.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
        }
        return 0;
    }

    if (command == "dumpaudio")
    {
        std::ofstream out(argv[3], std::ios::binary);
        for (unsigned int f = 0; f < maxFrames && decoder.decodeNextFrame(); ++f)
        {
            const auto& audio = decoder.getFrameAudio();
            out.write(reinterpret_cast<const char*>(audio.data()), audio.size() * 2);
        }
        return 0;
    }

    std::cerr << "unknown command: " << command << std::endl;
    return 1;
}
