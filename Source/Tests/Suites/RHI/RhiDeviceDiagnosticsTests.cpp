#include "Engine/Systems/Renderer/RHI/RhiDeviceDiagnostics.h"
#include "Tests/Framework/Test.h"

using namespace Swim;

SWIM_TEST("RHI.DeviceDiagnostics", "FirstOperationIsOwnedBoundedAndSticky")
{
	Rhi::DeviceDiagnostics diagnostics;
	SWIM_CHECK(!diagnostics.IsLost());
	SWIM_CHECK(diagnostics.Snapshot().Fault.Status == Rhi::DeviceFaultStatus::None);
	std::string operation(1024, 'A');
	SWIM_CHECK(diagnostics.TryRecordLoss(operation, -4));
	operation.assign("overwritten");
	SWIM_CHECK(!diagnostics.TryRecordLoss("second operation", -1));
	const auto report = diagnostics.Snapshot();
	SWIM_CHECK(report.Lost);
	SWIM_CHECK_EQUAL(report.Operation, std::string(192, 'A'));
	SWIM_CHECK_EQUAL(report.NativeResult, -4);
	SWIM_CHECK(report.Fault.Status == Rhi::DeviceFaultStatus::Pending);
	diagnostics.CompleteFaultCapture({ Rhi::DeviceFaultStatus::Unsupported, 0, {}, {} });
	diagnostics.CompleteFaultCapture({ Rhi::DeviceFaultStatus::Failed, 0, {}, {} });
	SWIM_CHECK(diagnostics.Snapshot().Fault.Status == Rhi::DeviceFaultStatus::Unsupported);
}
