#include "Engine/Systems/Replay/ReplayToolsInternal.h"

#include "Engine/Systems/Replay/ReplayAudio.h"
#include "Engine/Systems/Replay/ReplayEditing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <span>

namespace Swim::Replay::Internal
{

	NodeId RequireNode(const Json& args, const char* field)
	{
		NodeId id = NoNode;

		if (!ParseNodeId(args[field], id) || id == NoNode)
		{
			throw ToolError(std::string("missing or invalid '") + field + "' (a node id string)");
		}

		return id;
	}

	NodeId OptionalNode(const Json& args, const char* field)
	{
		NodeId id = NoNode;

		if (args.Contains(field) && !args[field].IsNull() && !ParseNodeId(args[field], id))
		{
			throw ToolError(std::string("invalid '") + field + "'");
		}

		return id;
	}

	Ease ReadEase(const Json& args)
	{
		Ease ease = Ease::Linear;

		if (args.Contains("ease") && !ParseEase(args["ease"].AsString(), ease))
		{
			throw ToolError("ease must be step, linear, smooth, in, out or spline");
		}

		return ease;
	}

	double ReadTime(const Json& args, const ReplayPlayer& player)
	{
		return args.Contains("time") ? args["time"].AsNumber() : player.GetTime();
	}

	std::optional<Quat> ReadRotation(const Json& args)
	{
		if (args.Contains("rotation"))
		{
			if (!args["rotation"].IsArray() || args["rotation"].Size() != 4)
			{
				throw ToolError("rotation is a quaternion [x, y, z, w]");
			}

			return QuatFromJson(args["rotation"]);
		}

		if (args.Contains("euler"))
		{
			const Vec3 e = Vec3FromJson(args["euler"]);
			return EulerRotation(e[0], e[1], e[2]);
		}

		return std::nullopt;
	}

	std::optional<Vec3> ReadVec3(const Json& args, const char* field)
	{
		if (!args.Contains(field))
		{
			return std::nullopt;
		}

		if (!args[field].IsArray() || args[field].Size() != 3)
		{
			throw ToolError(std::string(field) + " is [x, y, z]");
		}

		return Vec3FromJson(args[field]);
	}

	AssetRef ReadAssetRef(const Json& j)
	{
		AssetRef ref;

		if (!ParseAssetKind(j["kind"].AsString("mesh"), ref.Kind))
		{
			throw ToolError("asset kind must be mesh, texture, material, sound, effect or other");
		}

		ref.Key = j["key"].AsString();

		if (ref.Key.empty())
		{
			throw ToolError("asset key is empty");
		}

		return ref;
	}

	Json Ok(ReplayPlayer& player)
	{
		Json result = Json::MakeObject();
		result["ok"] = true;
		result["time"] = player.GetTime();
		return result;
	}

	std::filesystem::path Resolve(const Context& c, const std::string& path)
	{
		std::filesystem::path p(path);
		return p.is_relative() && !c.Desc.Root.empty() ? c.Desc.Root / p : p;
	}

	Json TimeSchema()
	{
		return Schema::Number("Timeline seconds (default: the current time).");
	}

	Json EaseSchema()
	{
		return Schema::Enum(
			{ "step", "linear", "smooth", "in", "out", "spline", "bezier" }, "How this key moves to the next (default linear).");
	}

	Json NodeSchema(std::string_view what)
	{
		return Schema::String(what);
	}

	std::vector<double> MarkerTimes(const ReplayProject& project, std::string_view kind, std::string_view name)
	{
		std::vector<double> times;

		for (const auto& m : project.Markers)
		{
			if ((kind.empty() || m.Kind == kind) && (name.empty() || m.Name == name))
			{
				times.push_back(m.Time);
			}
		}

		std::sort(times.begin(), times.end());
		return times;
	}

	double ReadTimeOrMarker(const Json& args, const char* field, const ReplayProject& project)
	{
		const Json& v = args[field];

		if (v.IsNumber())
		{
			return v.AsNumber();
		}

		if (v.IsString())
		{
			for (const auto& m : project.Markers)
			{
				if (m.Name == v.AsString())
				{
					return m.Time;
				}
			}

			throw ToolError(std::string("no marker named '") + v.AsString() + "' for '" + field + "'");
		}

		throw ToolError(std::string("'") + field + "' is a time in seconds or a marker name");
	}

	std::vector<Vec3> ReadPoints(const Json& args, const char* field)
	{
		std::vector<Vec3> points;

		for (const auto& p : args[field].GetArray())
		{
			if (!p.IsArray() || p.Size() != 3)
			{
				throw ToolError(std::string(field) + " is a list of [x, y, z]");
			}

			points.push_back(Vec3FromJson(p));
		}

		return points;
	}

	Json Times(const std::vector<double>& times, double offset)
	{
		Json list = Json::MakeArray();

		for (const double t : times)
		{
			list.Push(t + offset);
		}

		return list;
	}

	Json PathSummary(const std::string& name, const CameraPath& path)
	{
		Json j = path.ToJson();
		j["name"] = name;
		j["length"] = path.Length();
		return j;
	}

	Json ShotJson(const Shot& shot)
	{
		Json j = Json::MakeObject();
		j["name"] = shot.Name;
		j["camera"] = shot.Camera;
		j["start"] = shot.Start;
		j["end"] = shot.End;

		if (!shot.Path.empty())
		{
			j["path"] = shot.Path;
			j["progressFrom"] = shot.ProgressFrom;
			j["progressTo"] = shot.ProgressTo;
			j["progressEase"] = ToString(shot.ProgressEase);
		}

		return j;
	}

	Json EaseOrSmoothSchema()
	{
		return Schema::Enum({ "step", "linear", "smooth", "in", "out", "spline", "bezier" }, "Easing (default smooth).");
	}

	Ease ReadEaseOr(const Json& args, Ease fallback)
	{
		return args.Contains("ease") ? ReadEase(args) : fallback;
	}

	MusicCache& LoadMusic(Context& c, MusicCache& cache)
	{
		const auto& music = c.Player->GetProject().Music;

		if (!music || music->Path.empty())
		{
			throw ToolError("no music track (music.set first)");
		}

		if (cache.Path == music->Path && !cache.Mono.empty())
		{
			return cache;
		}

		if (!c.Desc.LoadMusic)
		{
			throw ToolError("this host cannot decode audio (ReplayToolsDesc::LoadMusic is unset)");
		}

		MusicCache fresh;
		fresh.Path = music->Path;
		std::string error;

		if (!c.Desc.LoadMusic(Resolve(c, music->Path), fresh.Mono, fresh.Rate, error) || fresh.Rate <= 0.0)
		{
			throw ToolError("cannot load music: " + error);
		}

		cache = std::move(fresh);
		return cache;
	}

} // namespace Swim::Replay::Internal
