#pragma once

// Swim Engine's first-party test framework.
//
// The whole engine test corpus is compiled into a single `SwimTests` program.
// Test cases self-register through static initializers, so adding coverage only
// requires dropping a `.cpp` into the correct `Source/Tests/Suites/<group>`
// directory and writing `SWIM_TEST("Suite", "Case")`. No central list, no extra
// CMake target, and no per-test `main()`.
//
// Checks never compile away: Swim defines NDEBUG in every configuration, so
// `assert()` is a no-op even in Debug builds. These macros always evaluate their
// expression and always report.

#include <chrono>
#include <cstddef>
#include <exception>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Swim::Testing
{

	struct SourceLocation
	{
		const char* File = "";
		int Line = 0;
	};

	struct TestFailure
	{
		SourceLocation Location{};
		std::string Expression;
		std::string Message;
	};

	// Records the outcome of exactly one running test case.
	//
	// A test may create worker threads and check from them (the job system and
	// deferred-command suites both do), so recording is synchronized. Only the
	// thread that owns the test may use the aborting `SWIM_REQUIRE*` forms,
	// because those unwind through the runner's catch handler.
	class TestContext
	{

	public:

		void RecordFailure(const SourceLocation& location, std::string expression, std::string message);
		void CountCheck();

		bool HasFailures() const;
		std::size_t GetCheckCount() const;
		std::vector<TestFailure> GetFailures() const;

	private:

		mutable std::mutex mutex;
		std::vector<TestFailure> failures;
		std::size_t checkCount = 0;

	};

	using TestFunction = void (*)();

	struct TestCase
	{
		std::string Suite;
		std::string Name;
		SourceLocation Location{};
		TestFunction Function = nullptr;

		std::string GetId() const;
	};

	// Process-wide registry populated before `main()` by static initializers.
	class TestRegistry
	{

	public:

		static TestRegistry& Get();

		void Add(TestCase test);
		const std::vector<TestCase>& GetTests() const;

	private:

		TestRegistry() = default;

		std::vector<TestCase> tests;

	};

	struct TestRegistrar
	{
		explicit TestRegistrar(TestCase test);
	};

	// Thrown by the aborting check forms. The runner catches it and treats the
	// already-recorded failure as the test result.
	class TestAbort final : public std::exception
	{

	public:

		const char* what() const noexcept override;

	};

	namespace Detail
	{

		// The runner installs the running test's context around each case. It is
		// deliberately not thread-local: worker threads a test spawns must observe
		// the same context.
		void SetCurrentContext(TestContext* context);
		TestContext* GetCurrentContext();

		[[noreturn]] void AbortCurrentTest();

		void ReportFailure(const SourceLocation& location, const char* expression, std::string message);
		void ReportPass();

		template <typename Value>
		concept Streamable = requires(std::ostream& stream, const Value& value)
		{
			stream << value;
		};

		template <typename Value>
		std::string Describe(const Value& value)
		{
			if constexpr (std::is_convertible_v<const Value&, std::string_view>)
			{
				return std::string(std::string_view(value));
			}
			else if constexpr (Streamable<Value>)
			{
				std::ostringstream stream;
				stream << value;
				return stream.str();
			}
			else
			{
				return "<unprintable>";
			}
		}

		template <typename Left, typename Right>
		std::string DescribeComparison(const Left& left, const Right& right)
		{
			return "left was " + Describe(left) + ", right was " + Describe(right);
		}

		// The describing callable is only invoked on failure so that checks inside
		// hot loops stay cheap.
		template <typename DescribeFailure>
		bool ReportCheck(bool passed, const SourceLocation& location, const char* expression, DescribeFailure&& describeFailure)
		{
			if (passed)
			{
				ReportPass();
				return true;
			}

			ReportFailure(location, expression, describeFailure());
			return false;
		}

	} // namespace Detail

} // namespace Swim::Testing

#define SWIM_TEST_CONCAT_INNER(left, right) left##right
#define SWIM_TEST_CONCAT(left, right) SWIM_TEST_CONCAT_INNER(left, right)

#define SWIM_TEST_LOCATION ::Swim::Testing::SourceLocation{ __FILE__, __LINE__ }

#define SWIM_TEST_DEFINE(suiteLiteral, nameLiteral, functionName, registrarName) \
	static void functionName(); \
	static const ::Swim::Testing::TestRegistrar registrarName \
	{ \
		::Swim::Testing::TestCase{ suiteLiteral, nameLiteral, SWIM_TEST_LOCATION, &functionName } \
	}; \
	static void functionName()

// Declares one test case. `suite` groups related cases and may itself be dotted
// (for example "AssetCompiler.GltfImporter"); the runner's filters match against
// the full "<suite>.<name>" identifier.
#define SWIM_TEST(suite, name) \
	SWIM_TEST_DEFINE( \
		suite, \
		name, \
		SWIM_TEST_CONCAT(SwimTestCase_, __LINE__), \
		SWIM_TEST_CONCAT(SwimTestRegistrar_, __LINE__))

// Records a failure and keeps going, so one run reports every broken expectation.
#define SWIM_CHECK(expression) \
	(void)::Swim::Testing::Detail::ReportCheck( \
		static_cast<bool>(expression), \
		SWIM_TEST_LOCATION, \
		#expression, \
		[] { return std::string{}; })

#define SWIM_CHECK_MESSAGE(expression, message) \
	(void)::Swim::Testing::Detail::ReportCheck( \
		static_cast<bool>(expression), \
		SWIM_TEST_LOCATION, \
		#expression, \
		[&] { return ::Swim::Testing::Detail::Describe(message); })

// Records a failure and abandons the rest of the case. Use it when continuing
// would crash or report meaningless follow-on failures.
#define SWIM_REQUIRE(expression) \
	do \
	{ \
		if (!::Swim::Testing::Detail::ReportCheck( \
			static_cast<bool>(expression), \
			SWIM_TEST_LOCATION, \
			#expression, \
			[] { return std::string{}; })) \
		{ \
			::Swim::Testing::Detail::AbortCurrentTest(); \
		} \
	} \
	while (false)

#define SWIM_REQUIRE_MESSAGE(expression, message) \
	do \
	{ \
		if (!::Swim::Testing::Detail::ReportCheck( \
			static_cast<bool>(expression), \
			SWIM_TEST_LOCATION, \
			#expression, \
			[&] { return ::Swim::Testing::Detail::Describe(message); })) \
		{ \
			::Swim::Testing::Detail::AbortCurrentTest(); \
		} \
	} \
	while (false)

#define SWIM_CHECK_EQUAL(left, right) \
	do \
	{ \
		const auto& swimCheckLeft = (left); \
		const auto& swimCheckRight = (right); \
		(void)::Swim::Testing::Detail::ReportCheck( \
			swimCheckLeft == swimCheckRight, \
			SWIM_TEST_LOCATION, \
			#left " == " #right, \
			[&] { return ::Swim::Testing::Detail::DescribeComparison(swimCheckLeft, swimCheckRight); }); \
	} \
	while (false)

#define SWIM_REQUIRE_EQUAL(left, right) \
	do \
	{ \
		const auto& swimRequireLeft = (left); \
		const auto& swimRequireRight = (right); \
		if (!::Swim::Testing::Detail::ReportCheck( \
			swimRequireLeft == swimRequireRight, \
			SWIM_TEST_LOCATION, \
			#left " == " #right, \
			[&] { return ::Swim::Testing::Detail::DescribeComparison(swimRequireLeft, swimRequireRight); })) \
		{ \
			::Swim::Testing::Detail::AbortCurrentTest(); \
		} \
	} \
	while (false)

#define SWIM_CHECK_NEAR(left, right, tolerance) \
	do \
	{ \
		const auto swimNearLeft = static_cast<double>(left); \
		const auto swimNearRight = static_cast<double>(right); \
		const auto swimNearTolerance = static_cast<double>(tolerance); \
		(void)::Swim::Testing::Detail::ReportCheck( \
			(swimNearLeft - swimNearRight <= swimNearTolerance) && (swimNearRight - swimNearLeft <= swimNearTolerance), \
			SWIM_TEST_LOCATION, \
			#left " ~= " #right, \
			[&] { return ::Swim::Testing::Detail::DescribeComparison(swimNearLeft, swimNearRight); }); \
	} \
	while (false)

// Proves a contract rejects bad input through the exact exception type it
// documents, rather than through any failure at all.
#define SWIM_CHECK_THROWS(expression, ExceptionType) \
	do \
	{ \
		bool swimCaughtExpected = false; \
		try \
		{ \
			(void)(expression); \
		} \
		catch (const ExceptionType&) \
		{ \
			swimCaughtExpected = true; \
		} \
		(void)::Swim::Testing::Detail::ReportCheck( \
			swimCaughtExpected, \
			SWIM_TEST_LOCATION, \
			#expression " throws " #ExceptionType, \
			[] { return std::string{}; }); \
	} \
	while (false)

#define SWIM_FAIL(message) \
	(void)::Swim::Testing::Detail::ReportCheck( \
		false, \
		SWIM_TEST_LOCATION, \
		"explicit failure", \
		[&] { return ::Swim::Testing::Detail::Describe(message); })
