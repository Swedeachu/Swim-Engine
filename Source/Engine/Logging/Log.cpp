#include "PCH.h"
#include "Engine/Logging/Log.h"

#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "Engine/Platform/Internal/WindowsApi.h"
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace Engine::Logging
{
	namespace
	{
		class SpdlogStreamBuffer final : public std::streambuf
		{
		public:
			SpdlogStreamBuffer(std::shared_ptr<spdlog::logger> logger, spdlog::level::level_enum level)
				: logger(std::move(logger)), level(level)
			{
			}

			~SpdlogStreamBuffer() override
			{
				sync();
			}

		protected:
			int_type overflow(int_type character) override
			{
				if (traits_type::eq_int_type(character, traits_type::eof()))
				{
					std::lock_guard<std::mutex> lock(mutex);
					EmitLocked();
					return traits_type::not_eof(character);
				}

				const char value = traits_type::to_char_type(character);
				std::lock_guard<std::mutex> lock(mutex);
				AppendLocked(&value, 1);
				return character;
			}

			std::streamsize xsputn(const char* data, std::streamsize count) override
			{
				if (!data || count <= 0)
				{
					return 0;
				}

				std::lock_guard<std::mutex> lock(mutex);
				AppendLocked(data, static_cast<std::size_t>(count));
				return count;
			}

			int sync() override
			{
				std::lock_guard<std::mutex> lock(mutex);
				EmitLocked();
				if (logger)
				{
					logger->flush();
				}
				return 0;
			}

		private:
			void AppendLocked(const char* data, std::size_t count)
			{
				for (std::size_t index = 0; index < count; ++index)
				{
					const char value = data[index];
					if (value == '\n')
					{
						EmitLocked();
					}
					else if (value != '\r')
					{
						line.push_back(value);
					}
				}
			}

			void EmitLocked()
			{
				if (line.empty() || !logger)
				{
					line.clear();
					return;
				}

				logger->log(level, line);
				line.clear();
			}

			std::shared_ptr<spdlog::logger> logger;
			spdlog::level::level_enum level = spdlog::level::info;
			std::mutex mutex;
			std::string line;
		};

		std::mutex stateMutex;
		std::shared_ptr<spdlog::logger> logger;
		std::unique_ptr<SpdlogStreamBuffer> coutBuffer;
		std::unique_ptr<SpdlogStreamBuffer> cerrBuffer;
		std::streambuf* originalCoutBuffer = nullptr;
		std::streambuf* originalCerrBuffer = nullptr;
		std::filesystem::path logFilePath;
		bool cerrWasUnitBuffered = false;
		bool initialized = false;

		std::filesystem::path ResolveExecutableDirectory()
		{
#if defined(_WIN32)
			std::wstring buffer(1024, L'\0');
			while (true)
			{
				const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (length == 0)
				{
					break;
				}
				if (length < buffer.size() - 1)
				{
					buffer.resize(length);
					return std::filesystem::path(buffer).parent_path();
				}
				buffer.resize(buffer.size() * 2);
			}
#elif defined(__linux__)
			std::vector<char> buffer(1024, '\0');
			while (true)
			{
				const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
				if (length < 0)
				{
					break;
				}
				if (static_cast<std::size_t>(length) < buffer.size())
				{
					return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(length))).parent_path();
				}
				buffer.resize(buffer.size() * 2);
			}
#endif
			std::error_code error;
			const std::filesystem::path current = std::filesystem::current_path(error);
			return error ? std::filesystem::path(".") : current;
		}

		std::string TimestampForFileName()
		{
			const auto now = std::chrono::system_clock::now();
			const std::time_t time = std::chrono::system_clock::to_time_t(now);
			std::tm local{};
#if defined(_WIN32)
			localtime_s(&local, &time);
#else
			localtime_r(&time, &local);
#endif
			std::ostringstream stream;
			stream << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
			return stream.str();
		}
	}

	bool Initialize()
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		if (initialized)
		{
			return true;
		}

		try
		{
			std::vector<spdlog::sink_ptr> sinks;
			auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			consoleSink->set_pattern("%^[%H:%M:%S.%e] [%l]%$ %v");
			sinks.push_back(consoleSink);

			const std::filesystem::path logsDirectory = ResolveExecutableDirectory() / "Logs";
			std::error_code directoryError;
			std::filesystem::create_directories(logsDirectory, directoryError);
			if (!directoryError)
			{
				logFilePath = logsDirectory / ("swim_engine_log_" + TimestampForFileName() + ".txt");
				try
				{
					auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
					fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
					sinks.push_back(fileSink);
				}
				catch (const spdlog::spdlog_ex&)
				{
					logFilePath.clear();
				}
			}

			logger = std::make_shared<spdlog::logger>("swim_engine", sinks.begin(), sinks.end());
#if defined(_SWIM_DEBUG)
			logger->set_level(spdlog::level::debug);
#else
			logger->set_level(spdlog::level::info);
#endif
			logger->flush_on(spdlog::level::info);
			spdlog::set_default_logger(logger);

			originalCoutBuffer = std::cout.rdbuf();
			originalCerrBuffer = std::cerr.rdbuf();
			cerrWasUnitBuffered = (std::cerr.flags() & std::ios::unitbuf) != 0;
			std::cerr.unsetf(std::ios::unitbuf);
			coutBuffer = std::make_unique<SpdlogStreamBuffer>(logger, spdlog::level::info);
			cerrBuffer = std::make_unique<SpdlogStreamBuffer>(logger, spdlog::level::err);
			std::cout.rdbuf(coutBuffer.get());
			std::cerr.rdbuf(cerrBuffer.get());

			initialized = true;
			if (logFilePath.empty())
			{
				logger->warn("File logging is unavailable; continuing with console logging only.");
			}
			else
			{
				logger->info("Logging to {}", logFilePath.string());
			}
			return true;
		}
		catch (const std::exception& error)
		{
			if (originalCoutBuffer)
			{
				std::cout.rdbuf(originalCoutBuffer);
			}
			if (originalCerrBuffer)
			{
				std::cerr.rdbuf(originalCerrBuffer);
			}
			if (cerrWasUnitBuffered)
			{
				std::cerr.setf(std::ios::unitbuf);
			}
			coutBuffer.reset();
			cerrBuffer.reset();
			logger.reset();
			std::cerr << "[Logging] Failed to initialize spdlog: " << error.what() << '\n';
			return false;
		}
	}

	void Flush()
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		if (!initialized)
		{
			return;
		}

		std::cout.flush();
		std::cerr.flush();
		if (logger)
		{
			logger->flush();
		}
	}

	void Shutdown()
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		if (!initialized)
		{
			return;
		}

		std::cout.flush();
		std::cerr.flush();
		if (originalCoutBuffer)
		{
			std::cout.rdbuf(originalCoutBuffer);
		}
		if (originalCerrBuffer)
		{
			std::cerr.rdbuf(originalCerrBuffer);
		}
		if (cerrWasUnitBuffered)
		{
			std::cerr.setf(std::ios::unitbuf);
		}
		coutBuffer.reset();
		cerrBuffer.reset();

		if (logger)
		{
			logger->flush();
		}
		spdlog::shutdown();
		logger.reset();
		originalCoutBuffer = nullptr;
		originalCerrBuffer = nullptr;
		cerrWasUnitBuffered = false;
		initialized = false;
	}

	const std::filesystem::path& GetLogFilePath()
	{
		return logFilePath;
	}

}
