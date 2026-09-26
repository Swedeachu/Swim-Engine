// Retired 2026-09-26: the sandbox control panel's "Findings" tab (a list of the hard-coded
// Game::GetFindings() entries with a detail label). The findings now live only in
// docs/EngineRuntime.md (synced from Source/Game/Findings.cpp by scripts/sync-findings-doc.py).
// Not compiled; kept for reference.

void SandboxHud::BuildFindings(UiNodeId parent)
{
	CreateLabel(*document, parent, "What building the runtime found:");
	std::vector<std::string> titles;
	for (const auto& finding : GetFindings())
	{
		titles.push_back("[" + std::string(finding.Status) + "] " + std::string(finding.Title));
	}
	UiStyle listStyle;
	listStyle.Width = UiLength::Percent(1.0f);
	listStyle.Height = UiLength::Pixels(250.0f);
	findingsList = CreateListView(*document, parent, listStyle, titles, 0);
	findingDetails = CreateLabel(*document, parent, "-");
	auto style = document->GetStyle(findingDetails);
	style.TextWrap = Swim::Text::TextWrap::Word;
	style.Width = UiLength::Percent(1.0f);
	document->SetStyle(findingDetails, style);
	bindings.OnValue(findingsList.Root,
		[this](float value)
		{
			const auto findings = GetFindings();
			const auto index = static_cast<std::int64_t>(value);
			if (index >= 0 && static_cast<std::size_t>(index) < findings.size())
			{
				SetLabelText(*document, findingDetails, std::string(findings[static_cast<std::size_t>(index)].Detail));
			}
		});
}
