#include "main.h"
#include "core/stb/stb.h"
#include "core/time/timer.h"

namespace fs = std::filesystem;

void decode(std::filesystem::path input, std::filesystem::path output)
{
	if (output.empty())
	{
		output = fs::path(input).replace_extension(texture_only ? "png" : ".json");
	}
	fs::path basepath = output.parent_path();

	SCTXSerializer serializer(input, true);

	SCTXSerializer::ImagesT images;
	serializer.save_serialized(output, images);

	for (auto& image : images)
	{
		fs::path image_path = fs::path(basepath) / image.name;
		wk::OutputFileStream stream(texture_only ? output : image_path);

		wk::stb::write_image(*image.image, wk::stb::ImageFormat::PNG, stream);

		// write only the first (level 0) texture
		if (texture_only) break;
	}
}

void encode(std::filesystem::path input, std::filesystem::path output)
{
	if (output.empty())
	{
		output = fs::path(input).replace_extension(".sctx");
	}

	SCTXSerializer::def_compression = compress_data;
	SCTXSerializer::def_padding = use_padding;
	SCTXSerializer::def_streaming = generate_streaming;

	SCTXSerializer serializer(input, false);
	serializer.save_binary(output, compress_data, use_padding);
}

void program(wk::ArgumentParser& args)
{
    fs::path input = args.get("input");
    fs::path output = args.get("output");
    std::string mode = args.get("mode");
    compress_data = args.get<bool>("compress-data");
    texture_only = args.get<bool>("texture-only");
    use_padding = args.get<bool>("use-padding");
    generate_streaming = args.get<bool>("generate-streaming");

    if (!fs::exists(input)) {
        std::cout << "Input path does not exist" << std::endl;
        return;
    }

    wk::Timer operation_timer;

    if (mode == "decode")
    {
        if (fs::is_directory(input))
        {
            fs::path dump_dir = input / "dump";
            if (!fs::exists(dump_dir))
                fs::create_directory(dump_dir);

            for (auto& entry : fs::directory_iterator(input))
            {
                if (!entry.is_regular_file())
                    continue;

                fs::path file_path = entry.path();
				if (file_path.extension() != ".sctx")
					continue;

                std::cout << "Decoding file: " << file_path.filename().string() << std::endl;

                try {
                    fs::path output_file = dump_dir / file_path.stem();
                    decode(file_path, output_file);
                }
                catch (const std::exception& e)
                {
                    std::cout << "Error decoding " << file_path.filename().string()
                              << ": " << e.what() << std::endl;
                }
            }
        }
        else
        {
            decode(input, output);
        }
    }
    else if (mode == "encode")
    {
        if (fs::is_directory(input)) {
            std::cout << "Encode mode does not support directory input" << std::endl;
            return;
        }

        encode(input, output);
    }

    std::cout << "Operation took: " << operation_timer.elapsed() / 1000.0 << " seconds" << std::endl;
}

int main(int argc, char* argv[])
{
	std::filesystem::path executable = argv[0];
	std::filesystem::path executable_name = executable.stem();

	wk::ArgumentParser parser(executable_name.string(), "Tool for compress and decompress Supercell Textures (SCTX)");

	parser.add_argument("mode")
		.help("Possible values: decode, encode")
		.choices("decode", "encode");

	parser.add_argument("input")
		.help("Path to sctx or info file (or dir for decode)");

	parser.add_argument("output")
		.default_value("")
		.help("Path to output sctx/info file/png file. Optional.");

	parser.add_argument("--compress-data", "-c")
		.flag()
		.help("Compress texture data using ZSTD when encoding texture");

	parser.add_argument("--use-padding", "-p")
		.flag()
		.help("Makes file 16 byte aligned");

	parser.add_argument("--texture-only", "-t")
		.flag()
		.help("Decompress only texture, without json file");

	parser.add_argument("--generate-streaming", "-t")
		.flag()
		.help("Generates streaming textures when encoding textures");

	try
	{
		parser.parse_args(argc, argv);
	}
	catch (const std::exception&)
	{
		std::cout << parser << std::endl;
		return 0;
	}

#ifndef _DEBUG
	try
	{
#endif
		program(parser);

#ifndef _DEBUG
	}
	catch (const std::exception& exception)
	{
		std::cout << exception.what() << std::endl;
		return 1;
	}
#endif	
	return 0;
}