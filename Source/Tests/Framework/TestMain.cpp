#include "Tests/Framework/TestRunner.h"

// The single entry point for the whole Swim Engine test corpus. Suites register
// themselves before this runs; the runner owns selection, execution, reporting,
// and the process exit code.
int main(int argc, char** argv)
{
	return Swim::Testing::RunTests(argc, argv);
}
