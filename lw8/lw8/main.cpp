#include <ctime>
#include <algorithm>
#include <vector>
#include <string>
#include <thread>
#include <sstream>
#include <Windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <queue>
#include <mutex>
#include <iostream>
#include <filesystem>
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

std::ostream& operator<<(std::ostream & strm, const RGBHolder & pixel)
{
	return strm << std::hex << static_cast<short>(pixel.R) << static_cast<short>(pixel.G) << static_cast<short>(pixel.B);
}

template<class T>
std::vector<std::vector<T>> Bunchify(const std::vector<T> & container, size_t bunchSize)
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

void ThrowInvalidCommandLineArguments()
{
	using namespace std::string_literals;
	throw std::exception(("Invalid command line arguments. Should be:\n"s
		+ "EXE <input_file_name> <output_file_name> <threads_count> <cores_count> <thread_priorities>\n"
		+ "Thread priorities:\n"
		+ "	0 - ABOVE_NORMAL\n"
		+ "	1 - BELOW_NORMAL\n"
		+ "	2 - HIGHEST\n"
		+ "	3 - IDLE\n"
		+ "	4 - LOWEST\n"
		+ "	5 - NORMAL\n"
		+ "	6 - TIME_CRTICIAL\n").c_str());
}

std::vector<BitHolder> BunchifyBitHolder(const BitHolder& bmp, size_t bunchSize)
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

int ToWinThreadPriority(char priority)
{
	switch (priority)
	{
	case 0:
		return THREAD_PRIORITY_ABOVE_NORMAL;
	case 1:
		return THREAD_PRIORITY_BELOW_NORMAL;
	case 2:
		return THREAD_PRIORITY_HIGHEST;
	case 3:
		return THREAD_PRIORITY_IDLE;
	case 4:
		return THREAD_PRIORITY_LOWEST;
	case 5:
		return THREAD_PRIORITY_NORMAL;
	case 6:
		return THREAD_PRIORITY_TIME_CRITICAL;
	default:
		throw std::exception("Unknown ThreadPriority");
	}
}

using Task = std::function<void()>;

class ThreadPool
{
public:
	ThreadPool(const std::vector<char>& priorities, size_t coresCount)
	{
		for (const auto priority : priorities)
		{
			m_threads.emplace_back([&]() {
				while (m_keepAlive)
				{
					if (const auto task = GetNextTask(); task)
					{
						++m_tasksInProgress;
						task.value()();
						--m_tasksInProgress;
					}
				}
				});
			SetThreadAffinityMask(m_threads.back().native_handle(), (1 << coresCount) - 1);
			SetThreadPriority(m_threads.back().native_handle(), ToWinThreadPriority(priority));
		}
	}

	void AddTasks(std::queue<Task>&& tasks)
	{
		std::lock_guard lock(m_tasksMutex);
		while (!tasks.empty())
		{
			AddTask(std::move(tasks.front()));
			tasks.pop();
		}
	}

	void AddTask(Task&& task)
	{
		std::lock_guard lock(m_tasksMutex);
		m_tasks.push(std::move(task));
	}

	void Join()
	{
		while (Wait());
	}

	~ThreadPool()
	{
		m_keepAlive = false;
		for_each(m_threads.begin(), m_threads.end(), std::mem_fn(&std::thread::join));
	}

private:
	std::optional<Task> GetNextTask()
	{
		std::lock_guard lock(m_tasksMutex);
		if (m_tasks.empty())
		{
			return std::nullopt;
		}

		const auto task = m_tasks.front();
		m_tasks.pop();
		return task;
	}

	bool Wait()
	{
		std::lock_guard lock(m_tasksMutex);
		return !m_tasks.empty() || m_tasksInProgress;
	}

	std::vector<std::thread> m_threads;
	std::queue<Task> m_tasks;
	std::mutex m_tasksMutex;
	std::atomic<size_t> m_tasksInProgress = 0;
	bool m_keepAlive = true;
};


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

std::vector<BitHolder> SeparateBitHolder(const BitHolder & bmp, size_t bunchSize)
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

bool IsNumber(const std::string_view & str)
{
	return std::all_of(str.begin(), str.end(), ::isdigit);
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

std::vector<char> PrioritiesOfThread(size_t threadsCount, size_t argc, char* argv[], int shift = 0)
{
	if (argc != (threadsCount + 5 + shift))
	{
		throw std::exception();
	}
	std::vector<char> threadPriorities;
	threadPriorities.reserve(threadsCount);
	for (size_t i = 5 + shift; i < argc; ++i)
	{
		threadPriorities.push_back(std::stoi(argv[i]));
	}
	return threadPriorities;
}

BitHolder BlurBitHolder(const BitHolder & bmp)
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

bool Mode(std::string_view str)
{
	if (str == "tp")
	{
		return true;
	}
	else if (str == "ntp")
	{
		return false;
	}
	throw std::exception((std::string("Unknown processing mode: ") + str.data()).c_str());
}


template <class T>
void Blur(std::string_view imgInName, std::string_view imgOutName, size_t blockCount, T&& blurStrategy)
{
	auto [pixels, width, height] = LoadPixels(imgInName);
	BitHolder bmp(pixels, width, height);

	auto subBitmaps = BunchifyBitHolder(bmp, blockCount);
	blurStrategy(subBitmaps);

	const auto bluredBitmap = MergeBitHolder(std::move(subBitmaps), bmp.Width(), bmp.Height());
	UnloadPixels(bluredBitmap.Pixels(), bmp.Width(), bmp.Height(), imgOutName.data());
}

std::function<void(std::vector<BitHolder>&)> Strategy(bool mode, size_t coresCount, const std::vector<char>& threadPriorities)
{
	static ThreadPool pool(threadPriorities, coresCount);
	switch (mode)
	{
	case true:
		return [](std::vector<BitHolder> & subBitmaps) {
			for (size_t i = 0; i < subBitmaps.size(); ++i)
			{
				pool.AddTask([&subBitmaps, i]() {
					subBitmaps[i] = BlurBitHolder(subBitmaps[i]);
					});
			}
			pool.Join();
		};
	case false:
		return [&threadPriorities, coresCount](std::vector<BitHolder> & subBitmaps) {
			std::vector<std::thread> threads;
			threads.reserve(threadPriorities.size());
			for (size_t i = 0; i < subBitmaps.size(); ++i)
			{
				threads.emplace_back([&subBitmaps, i]() {
					subBitmaps[i] = BlurBitHolder(subBitmaps[i]);
					});
				SetThreadAffinityMask(threads.back().native_handle(), (1 << coresCount) - 1);
				SetThreadPriority(threads.back().native_handle(), ToWinThreadPriority(threadPriorities[i]));
			}
			for_each(threads.begin(), threads.end(), std::mem_fn(&std::thread::join));
		};
	default:
		throw std::exception("Unexpected ProcessingMode");
	}
}

void BlurImg(std::string_view inputPath, std::string_view outputPath, bool mode, size_t blockCount, size_t coresCount, const std::vector<char>& threadPriorities)
{
	if (!std::filesystem::exists(inputPath))
	{
		throw std::exception("This directory does not exist");
	}

	if (!std::filesystem::exists(outputPath))
	{
		std::filesystem::create_directories(outputPath);
	}

	const auto blurStrategy = Strategy(mode, coresCount, threadPriorities);
	for (const auto& file : std::filesystem::directory_iterator(inputPath))
	{
		const auto path = file.path();
		if (path.extension() == ".bmp")
		{
			const auto input = path;
			const auto output = outputPath / path.filename();
			Blur(input.string(), output.string(), blockCount, blurStrategy);
		}
	}
}

int main(int argc, char* argv[])
{
	try
	{
		if (argc < 7)
		{
			ThrowInvalidCommandLineArguments();
		}
		const std::string dirIn = argv[1];
		const std::string dirOut = argv[2];
		const auto begin = std::clock();
		const auto processingMode = Mode(argv[3]);
		const auto blocks = ExtractPositiveNumber(argv[4]);
		const auto threads = ExtractPositiveNumber(argv[5]);
		const auto cores = ExtractPositiveNumber(argv[6]);
		std::vector<char> threadPriorities;
		try
		{
			threadPriorities = std::move(PrioritiesOfThread(threads, argc, argv, +2));
		}
		catch (const std::exception&)
		{
			ThrowInvalidCommandLineArguments();
		}
		BlurImg(dirIn, dirOut, processingMode, blocks, cores, threadPriorities);
		std::cerr << std::difftime(std::clock(), begin) << "ms" << std::endl;
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
