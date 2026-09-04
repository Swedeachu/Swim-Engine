#include "Tests/Framework/Test.h"

#include <algorithm>
#include <stdexcept>

namespace Swim::Testing
{

	namespace
	{

		TestContext* CurrentContext = nullptr;

	}

	void TestContext::RecordFailure(const SourceLocation& location, std::string expression, std::string message)
	{
		const std::lock_guard<std::mutex> lock(mutex);
		++checkCount;
		failures.push_back(TestFailure{ location, std::move(expression), std::move(message) });
	}

	void TestContext::CountCheck()
	{
		const std::lock_guard<std::mutex> lock(mutex);
		++checkCount;
	}

	bool TestContext::HasFailures() const
	{
		const std::lock_guard<std::mutex> lock(mutex);
		return !failures.empty();
	}

	std::size_t TestContext::GetCheckCount() const
	{
		const std::lock_guard<std::mutex> lock(mutex);
		return checkCount;
	}

	std::vector<TestFailure> TestContext::GetFailures() const
	{
		const std::lock_guard<std::mutex> lock(mutex);
		return failures;
	}

	std::string TestCase::GetId() const
	{
		if (Suite.empty())
		{
			return Name;
		}
		return Suite + "." + Name;
	}

	TestRegistry& TestRegistry::Get()
	{
		// Function-local storage keeps registration safe regardless of the static
		// initialization order across the suite translation units.
		static TestRegistry registry;
		return registry;
	}

	void TestRegistry::Add(TestCase test)
	{
		tests.push_back(std::move(test));
	}

	const std::vector<TestCase>& TestRegistry::GetTests() const
	{
		return tests;
	}

	TestRegistrar::TestRegistrar(TestCase test)
	{
		TestRegistry::Get().Add(std::move(test));
	}

	const char* TestAbort::what() const noexcept
	{
		return "Swim test aborted by a failed requirement";
	}

	namespace Detail
	{

		void SetCurrentContext(TestContext* context)
		{
			CurrentContext = context;
		}

		TestContext* GetCurrentContext()
		{
			return CurrentContext;
		}

		void AbortCurrentTest()
		{
			throw TestAbort{};
		}

		void ReportFailure(const SourceLocation& location, const char* expression, std::string message)
		{
			TestContext* context = CurrentContext;
			if (context == nullptr)
			{
				// A check outside a running test is a framework misuse, not a test
				// result, so it must not be silently swallowed.
				throw std::logic_error("Swim test check evaluated outside of a running test case");
			}

			context->RecordFailure(location, expression, std::move(message));
		}

		void ReportPass()
		{
			TestContext* context = CurrentContext;
			if (context == nullptr)
			{
				throw std::logic_error("Swim test check evaluated outside of a running test case");
			}

			context->CountCheck();
		}

	} // namespace Detail

} // namespace Swim::Testing
