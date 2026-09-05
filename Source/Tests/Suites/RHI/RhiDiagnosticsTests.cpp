#include "Engine/Systems/Renderer/RHI/RhiDiagnostics.h"
#include "Tests/Framework/Test.h"

#include <array>
#include <thread>

using namespace Swim;

SWIM_TEST("RHI.Diagnostics", "OwnsMessagesBoundsCopiesAndDoesNotHideOverflow")
{
	Rhi::DiagnosticLog log(2);
	std::string text = "original";
	log.Record(Rhi::DiagnosticSeverity::Info, "id", text);
	text = "changed";
	log.Record(Rhi::DiagnosticSeverity::Warning, std::string(300, 'I'), std::string(9000, 'T'));
	log.Record(Rhi::DiagnosticSeverity::Error, "overflow", "must still count");
	const auto snapshot = log.Snapshot();
	SWIM_REQUIRE_EQUAL(snapshot.Messages.size(), 2u);
	SWIM_CHECK_EQUAL(snapshot.Messages[0].Text, std::string("original"));
	SWIM_CHECK_EQUAL(snapshot.Messages[1].Id.size(), 256u);
	SWIM_CHECK_EQUAL(snapshot.Messages[1].Text.size(), 8192u);
	SWIM_CHECK_EQUAL(snapshot.Warnings, 1u);
	SWIM_CHECK_EQUAL(snapshot.Errors, 1u);
	SWIM_CHECK_EQUAL(snapshot.Dropped, 1u);
	SWIM_CHECK(!snapshot.IsClean());
}

SWIM_TEST("RHI.Diagnostics", "ConcurrentCallbacksKeepCountsWhenStorageIsFull")
{
	Rhi::DiagnosticLog log(32);
	std::array<std::thread, 4> workers;
	for (unsigned index = 0; index < workers.size(); ++index)
	{
		workers[index] = std::thread([&, index]
		{
			for (unsigned record = 0; record < 250; ++record)
			{
				log.Record(index % 2 ? Rhi::DiagnosticSeverity::Error : Rhi::DiagnosticSeverity::Warning, "concurrent", "message");
			}
		});
	}
	for (auto& worker : workers)
	{
		worker.join();
	}
	const auto snapshot = log.Snapshot();
	SWIM_CHECK_EQUAL(snapshot.Messages.size(), 32u);
	SWIM_CHECK_EQUAL(snapshot.Warnings, 500u);
	SWIM_CHECK_EQUAL(snapshot.Errors, 500u);
	SWIM_CHECK_EQUAL(snapshot.Dropped, 968u);
}

SWIM_TEST("RHI.Diagnostics", "CleanSnapshotRequiresNoWarningsErrorsOrLostMessages")
{
	Rhi::DiagnosticLog log;
	SWIM_CHECK(log.Snapshot().IsClean());
	log.Record(Rhi::DiagnosticSeverity::Info, "adapter", "selected adapter");
	auto snapshot = log.Snapshot();
	SWIM_CHECK(snapshot.IsClean());
	snapshot.Messages[0].Text = "local copy";
	const auto retained = log.Snapshot();
	SWIM_CHECK_EQUAL(retained.Messages[0].Text, std::string("selected adapter"));
	Rhi::DiagnosticLog noStorage(0);
	noStorage.Record(Rhi::DiagnosticSeverity::Info, "lost", "lost");
	SWIM_CHECK(!noStorage.Snapshot().IsClean());
	SWIM_CHECK_EQUAL(noStorage.Snapshot().Dropped, 1u);
}
