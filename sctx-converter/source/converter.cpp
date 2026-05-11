#include "converter.h"
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;
namespace fs = std::filesystem;

constexpr auto kTexture = "texture";
constexpr auto kTextures = "textures";
constexpr auto kPixelType = "type";
constexpr auto kGenerateMips = "generate_mip_maps";
constexpr auto kCompression = "compressed";

constexpr auto kUnkFlag1 = "unknown_flag";
constexpr auto kUnkFlag2 = "unknown_flag_1";

constexpr auto kProxyTextures = "proxy_textures";
constexpr auto kAstcParams = "astc_encode_params";
constexpr auto kTags = "tags";

SCTXSerializer::SCTXSerializer(fs::path path, bool is_binary)
{
	if (is_binary)
	{
		load_binary(path);
	}
	else
	{
		if (path.extension() == ".json")
		{
			load_serialized(path);
		}
		else
		{
			load_default_image(path);
		}

	}
}

void SCTXSerializer::load_default_image(std::filesystem::path path)
{
	using namespace sc::texture;

	wk::Ref<wk::RawImage> texture;
	wk::InputFileStream texture_file(path);

	fs::path extension = path.extension();
	ScPixel::Type target_type = SCTXSerializer::def_pixel_type;
	bool generate_mip_maps = SCTXSerializer::def_generate_mip_maps;
	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".psd" || extension == ".tga" || extension == ".bmp")
	{
		wk::stb::load_image(texture_file, texture);
	}
	else if (extension == ".ktx")
	{
		auto file = KhronosTexture::load_texture(texture_file);
		texture = wk::CreateRef<wk::RawImage>(file->width(), file->height(), file->depth(), file->colorspace());
		wk::SharedMemoryStream texture_data(texture->data(), texture->data_length());
		file->decompress_data(texture_data);
		generate_mip_maps = file->level_count() > 1;
		target_type = ScPixel::from_gl_format(file->internal_format());
	}

	m_texture_file.reset();
	m_texture = wk::CreateRef<SupercellTexture>(*texture, target_type, generate_mip_maps);
	m_texture->unknown_flag2 = true;
	m_texture->use_padding = SCTXSerializer::def_padding;
	m_texture->use_compression = SCTXSerializer::def_compression;
}

void SCTXSerializer::load_binary(std::filesystem::path path)
{
	m_texture_file = wk::CreateRef<wk::InputFileStream>(path);
	m_texture = wk::CreateRef<sc::texture::SupercellTexture>(*m_texture_file);
	m_texture->read_data();
}

void SCTXSerializer::load_serialized(std::filesystem::path path)
{
	using namespace sc::texture;

	std::ifstream file(path);
	json data = json::parse(file);

	fs::path working_dir = path.parent_path();
	ScPixel::Type pixel_type = ScPixel::from_string(data[kPixelType]);
	wk::Ref<wk::RawImage> streaming_texture;

	{
		fs::path texture_path = fs::path(working_dir) / data[kTexture];
		bool generate_mips = data[kGenerateMips];

		wk::Ref<wk::RawImage> texture;
		wk::InputFileStream texture_file(texture_path);
		wk::stb::load_image(texture_file, texture);
		if (SCTXSerializer::def_proxy) {
			streaming_texture = wk::CreateRef<wk::RawImage>(
				(uint16_t)std::floor((float)texture->width() / 4), (uint16_t)std::floor((float)texture->height() / 4),
				texture->depth(), texture->colorspace()
			);
			texture->copy(*streaming_texture);
		}

		m_texture_file.reset();
		m_texture = wk::CreateRef<SupercellTexture>(*texture, pixel_type, generate_mips);
		m_texture->unknown_flag1 = data[kUnkFlag1];
		m_texture->unknown_flag2 = data[kUnkFlag2];
	}

	auto& tags = data[kTags];
	if (tags.is_array())
	{
		m_texture->tags = tags.get<std::vector<std::string>>();
	}

	auto& astc_params = data[kAstcParams];
	if (astc_params.is_string())
	{
		m_texture->astc_encode_params = astc_params.get<std::string>();
	}

	auto& proxy_textures = data[kProxyTextures];
	if (proxy_textures.is_array() && !SCTXSerializer::def_proxy)
	{
		size_t proxy_count = proxy_textures.size();
		m_texture->proxy_textures.reserve(proxy_count);

		for (size_t i = 0; proxy_count > i; i++)
		{
			auto& proxy_texture = proxy_textures[i];

			fs::path texture_path = fs::path(working_dir) / proxy_texture[kTexture];
			ScPixel::Type variant_type = ScPixel::from_string(data[kPixelType]);

			wk::Ref<wk::RawImage> texture;
			wk::InputFileStream texture_file(texture_path);
			wk::stb::load_image(texture_file, texture);
			m_texture->proxy_textures.emplace_back(*texture, variant_type, false);
		}
	}
	else if (SCTXSerializer::def_proxy) {
		m_texture->proxy_textures.emplace_back(*streaming_texture, pixel_type, false);
	}
}

void SCTXSerializer::save_serialized(std::filesystem::path path, ImagesT& images)
{
	using namespace sc::texture;

	fs::path basename = fs::path(path).stem();
	json data = json::object();
	
	// Basic texture data
	{
		ImageInstance& image = images.emplace_back();
		image.name = basename.string().append(SCTXSerializer::image_postfix);
		SCTXSerializer::decode_texture(*m_texture, image.image);
		data[kTexture] = image.name;
		data[kPixelType] = ScPixel::to_string(m_texture->pixel_type());
		data[kGenerateMips] = m_texture->level_count() > 1;
		data[kUnkFlag1] = m_texture->unknown_flag1;
		data[kUnkFlag2] = m_texture->unknown_flag2;
	}

	// Texture extensions
	auto proxy_textures = json::array();
	if (!m_texture->proxy_textures.empty())
	{
		for (size_t i = 0; m_texture->proxy_textures.size() > i; i++)
		{
			auto& proxy = m_texture->proxy_textures[i];
			ImageInstance& image = images.emplace_back();
			image.name = basename.string().append("_variant_").append(std::to_string(i)).append(SCTXSerializer::image_postfix);
			SCTXSerializer::decode_texture(proxy, image.image);

			json proxy_info = json::object();
			proxy_info[kTexture] = image.name;
			proxy_info[kPixelType] = ScPixel::to_string(proxy.pixel_type());
			proxy_textures.push_back(proxy_info);
		}
	}

	data[kTags] = m_texture->tags;
	data[kAstcParams] = m_texture->astc_encode_params;
	data[kProxyTextures] = proxy_textures;
	
	wk::OutputFileStream stream(path);
	auto result = data.dump(2);
	stream.write(result.data(), result.size());
}

void SCTXSerializer::save_binary(std::filesystem::path path, bool save_compressed, bool save_padded)
{
	wk::OutputFileStream file(path);

	m_texture->use_compression = save_compressed;
	m_texture->use_padding = save_padded;
	m_texture->write(file);
}

void SCTXSerializer::decode_texture(sc::texture::SupercellTexture& texture, wk::Ref<wk::RawImage>& image)
{
	wk::Image::PixelDepth depth;
	wk::Image::ColorSpace space;
	try
	{
		depth = texture.depth();
		space = texture.colorspace();
	}
	catch (const wk::Exception&)
	{
		throw wk::Exception("Pixel type is not supported for decoding");
	}

	if (texture.is_compressed())
	{
		image = wk::CreateRef<wk::RawImage>(texture.width(), texture.height(), depth, space);
		wk::SharedMemoryStream stream(image->data(), image->data_length());
		texture.decompress_data(stream);
	}
	else
	{
		image = wk::CreateRef<wk::RawImage>(
			texture.data(), 
			texture.width(), texture.height(), depth, space
		);
	}
}