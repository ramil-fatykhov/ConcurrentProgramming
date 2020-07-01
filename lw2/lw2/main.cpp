#include <ctime>
#include <algorithm>
#include <vector>
#include <string>
#include <thread>
#include <Windows.h>
#include <string>
#include <functional>
#include <iostream>
#include <optional>
#include <numeric>
#include <ostream>
#include <fstream>
#include <vector>
#include <array>
#pragma warning(disable : 4714)
#pragma warning(disable : 4244)
#pragma warning(push, 0)
#include <boost/gil/extension/io/bmp.hpp>
#pragma warning(pop)

struct RGBHolder
{
	friend std::ostream& operator<<(std::ostream& strm, const RGBHolder& pixel);

	uint8_t R;
	uint8_t G;
	uint8_t B;
};

std::ostream& operator<<(std::ostream& strm, const RGBHolder& pixel)
{
	return strm << std::hex << static_cast<short>(pixel.R) << static_cast<short>(pixel.G) << static_cast<short>(pixel.B);
}

template<class T>
std::vector<std::vector<T>> Bunchify(const std::vector<T>& container, size_t bunchSize)
{
	std::vector<std::vector<T>> bunches;
	for (size_t i = 0; i < container.size(); i += bunchSize)
	{
		bunches.emplace_back(container.cbegin() + i, container.cbegin() + min(container.size(), i + bunchSize));
	}
	return bunches;
}

class BitHolder
{
public:
	using BitHolderPixel = RGBHolder;
	BitHolder(std::vector<RGBHolder> pixels, size_t width, size_t height)
		:m_pixels(pixels)
		, m_width(width)
		, m_height(height)
	{
		if (((width * height) != pixels.size()) || !width || !height)
		{
			throw std::exception("Invalid dimensions");
		}
	}

	const std::vector<RGBHolder>& Pixels() const
	{
		return m_pixels;
	}

	RGBHolder Pixel(size_t x, size_t y) const
	{
		return m_pixels[y * m_width + x];
	}

	size_t Width() const
	{
		return m_width;
	}

	size_t Height() const
	{
		return m_height;
	}

private:
	std::vector<BitHolderPixel> m_pixels;
	size_t m_width;
	size_t m_height;
};

struct ImageInfo
{
	std::vector<RGBHolder> pixels;
	size_t width;
	size_t height;
};

ImageInfo LoadPixels(std::string_view imgInName)
{
	using namespace::boost::gil;

	rgb8_image_t img;
	read_image(imgInName.data(), img, bmp_tag());

	std::vector<RGBHolder> pixels;
	pixels.reserve(img.width() * img.height() * num_channels<rgb8_image_t>());
	for_each_pixel(const_view(img), [&pixels](boost::gil::rgb8_pixel_t p) {
		pixels.push_back({
			at_c<0>(p),
			at_c<1>(p),
			at_c<2>(p)
			});
		});
	return {
		pixels,
		static_cast<size_t>(img.width()),
		static_cast<size_t>(img.height())
	};
}

void UnloadPixels(std::vector<RGBHolder> pixels, size_t width, size_t height, std::string_view imgOutName)
{
	using namespace::boost::gil;

	rgb8_image_t img(width, height);

	rgb8_image_t::view_t v = view(img);
	for (size_t y = 0; y < height; ++y)
	{
		for (size_t x = 0; x < width; ++x)
		{
			const auto& pixel = pixels[y * width + x];
			v(x, y) = rgb8_pixel_t(pixel.R, pixel.G, pixel.B);
		}
	}

	write_view(imgOutName.data(), const_view(img), bmp_tag());
}

BitHolder MergeBitHolder(std::vector<BitHolder> && bitmaps, size_t width, size_t height)
{
	std::vector<RGBHolder> totalBluredPixels;
	totalBluredPixels.reserve(width * height);
	for (const auto& bluredBitmap : bitmaps)
	{
		const auto& bluredPixels = bluredBitmap.Pixels();
		totalBluredPixels.insert(totalBluredPixels.end(), std::make_move_iterator(bluredPixels.cbegin()), std::make_move_iterator(bluredPixels.cend()));
	}
	return { std::move(totalBluredPixels), width, height };
}

std::vector<BitHolder> SeparateBitHolder(const BitHolder& bmp, size_t bunchSize)
{
	const size_t processingHeight = bmp.Height() / bunchSize;

	std::vector<BitHolder> bunches;
	bunches.reserve(processingHeight);
	for (const auto& bunch : Bunchify(bmp.Pixels(), processingHeight* bmp.Width()))
	{
		bunches.emplace_back(bunch, bmp.Width(), bunch.size() / bmp.Width());
	}
	return bunches;
}

void SetThreadAffinityMask(std::thread & thread, size_t coresCount)
{
	SetThreadAffinityMask(thread.native_handle(), (1 << coresCount) - 1);
}

bool IsNumber(const std::string_view & str)
{
	return std::all_of(str.begin(), str.end(), std::isdigit);
}

size_t ExtractPositiveNumber(std::string_view arg)
{
	try
	{
		if (!IsNumber(arg))
		{
			throw std::exception();
		}
		const auto value = std::stoi(arg.data());
		if (value <= 0)
		{
			throw std::exception();
		}
		return value;
	}
	catch (...)
	{
		throw std::exception(("'" + std::string(arg.data()) + "' should be a positive number").c_str());
	}
}

BitHolder BlurBitHolder(const BitHolder& bmp)
{
	std::vector<RGBHolder> pixels;
	pixels.reserve(bmp.Width() * bmp.Height());

	const auto getPixel = [](const BitHolder & bmp, size_t x, size_t y) -> std::optional<RGBHolder> {
		if ((x >= bmp.Width()) || (y >= bmp.Height()))
		{
			return std::nullopt;
		}
		return bmp.Pixel(x, y);
	};

	for (size_t y = 0; y < bmp.Height(); ++y)
	{
		for (size_t x = 0; x < bmp.Width(); ++x)
		{
			constexpr size_t matrixOrder = 9;

			std::array<std::optional<RGBHolder>, matrixOrder> optPixels = {
				getPixel(bmp, x - 1, y - 1), getPixel(bmp, x, y - 1), getPixel(bmp, x + 1, y - 1),
				getPixel(bmp, x - 1, y), getPixel(bmp, x, y), getPixel(bmp, x + 1, y),
				getPixel(bmp, x - 1, y + 1), getPixel(bmp, x, y + 1), getPixel(bmp, x + 1, y + 1),
			};
			const auto nulloptCount = count(optPixels.cbegin(), optPixels.cend(), std::nullopt);
			const auto r = accumulate(optPixels.cbegin(), optPixels.cend(), static_cast<size_t>(0), [](size_t acc, const auto & pixel) {
				return acc + (pixel ? (*pixel).R : 0);
				});
			const auto g = accumulate(optPixels.cbegin(), optPixels.cend(), static_cast<size_t>(0), [](size_t acc, const auto & pixel) {
				return acc + (pixel ? (*pixel).G : 0);
				});
			const auto b = accumulate(optPixels.cbegin(), optPixels.cend(), static_cast<size_t>(0), [](size_t acc, const auto & pixel) {
				return acc + (pixel ? (*pixel).B : 0);
				});
			pixels.push_back({
				static_cast<uint8_t>(r / (matrixOrder - nulloptCount)),
				static_cast<uint8_t>(g / (matrixOrder - nulloptCount)),
				static_cast<uint8_t>(b / (matrixOrder - nulloptCount))
				});
		}
	}
	return { pixels, bmp.Width(), bmp.Height() };
}


void BlurImg(std::string_view imgInName, std::string_view imgOutName, size_t threadsCount, size_t coresCount)
{
	auto [pixels, width, height] = LoadPixels(imgInName);
	BitHolder bmp(pixels, width, height);

	auto subBitmaps = SeparateBitHolder(bmp, threadsCount);
	std::vector<std::thread> threads;
	for (size_t i = 0; i < subBitmaps.size(); ++i)
	{
		threads.emplace_back([&subBitmaps, i]() {
			subBitmaps[i] = BlurBitHolder(subBitmaps[i]);
			});
		SetThreadAffinityMask(threads.back(), coresCount);
	}
	for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));

	const auto bluredBitmap = MergeBitHolder(std::move(subBitmaps), bmp.Width(), bmp.Height());
	UnloadPixels(bluredBitmap.Pixels(), bmp.Width(), bmp.Height(), imgOutName.data());
}

int main(int argc, char* argv[])
{
	try
	{
		if (argc != 5)
		{
			throw std::exception("Invalid command line arguments. Should be:\nEXE <input_file_name> <output_file_name> <threads_count> <cores_count>");
		}
		const auto begin = std::clock();
		const std::string imgIn = argv[1];
		const std::string imgOut = argv[2];
		const auto threadsCount = ExtractPositiveNumber(argv[3]);
		const auto coresCount = ExtractPositiveNumber(argv[4]);
		BlurImg(imgIn, imgOut, threadsCount, coresCount);
		std::cerr << std::difftime(std::clock(), begin) << "" << std::endl;
	}
	catch (const std::exception & ex)
	{
		std::cerr << ex.what() << std::endl;
	}
	catch (...)
	{
		std::cerr << "Unhandled non-STL exception" << std::endl;
	}
}
