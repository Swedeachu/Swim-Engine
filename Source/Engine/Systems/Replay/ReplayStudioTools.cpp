// Preview, visualisation and sequence tools (registered by RegisterReplayTools).

#include "Engine/Systems/Replay/ReplayTools.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace Swim::Replay
{

	using Tooling::Json;
	using Tooling::ToolError;
	namespace Schema = Tooling::Schema;

	namespace
	{

		struct PreviewContext
		{
			ReplayPlayer* Player = nullptr;
			ReplayToolsDesc Desc;
			std::atomic<std::uint64_t> Counter{ 0 };
		};

		Json PoseJson(const CameraPose& pose)
		{
			Json j = Json::MakeObject();
			j["camera"] = pose.Camera;
			j["shot"] = pose.Shot;
			j["position"] = ToJson(pose.Position);
			j["rotation"] = ToJson(pose.Rotation);
			j["fov"] = pose.Fov;
			j["valid"] = pose.Valid;
			return j;
		}

		Json FrustumJson(const CameraFrustum& f)
		{
			Json j = Json::MakeObject();
			j["apex"] = ToJson(f.Apex);
			j["forward"] = ToJson(f.Forward);
			j["up"] = ToJson(f.Up);
			j["right"] = ToJson(f.Right);
			Json nearCorners = Json::MakeArray();
			Json farCorners = Json::MakeArray();

			for (std::size_t i = 0; i < 4; ++i)
			{
				nearCorners.Push(ToJson(f.Near[i]));
				farCorners.Push(ToJson(f.Far[i]));
			}

			j["near"] = std::move(nearCorners);
			j["far"] = std::move(farCorners);
			return j;
		}

		Json Points(const std::vector<Vec3>& points)
		{
			Json list = Json::MakeArray();

			for (const Vec3& p : points)
			{
				list.Push(ToJson(p));
			}

			return list;
		}

		bool Finite(const Vec3& v)
		{
			return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
		}

		Vec3 ReadVec(const Json& args, const char* field, Vec3 fallback)
		{
			if (!args.Contains(field))
			{
				return fallback;
			}

			if (!args[field].IsArray() || args[field].Size() != 3)
			{
				throw ToolError(std::string(field) + " is [x, y, z]");
			}

			const Vec3 v = Vec3FromJson(args[field]);

			if (!Finite(v))
			{
				throw ToolError(std::string(field) + " must be finite");
			}

			return v;
		}

		// The camera at a time, with any explicit pose fields of the arguments applied.
		CameraPose ReadPose(ReplayPlayer& player, const Json& args, double time)
		{
			CameraPose pose = player.Peek(time).Camera;
			const bool explicitPose = args.Contains("position") || args.Contains("rotation") || args.Contains("euler") ||
				args.Contains("lookAt") || args.Contains("fov");

			if (explicitPose)
			{
				pose.Position = ReadVec(args, "position", pose.Position);

				if (args.Contains("lookAt"))
				{
					pose.Rotation = LookRotation(pose.Position, ReadVec(args, "lookAt", pose.Position), args["roll"].AsFloat(0.0f));
				}
				else if (args.Contains("rotation"))
				{
					if (!args["rotation"].IsArray() || args["rotation"].Size() != 4)
					{
						throw ToolError("rotation is a quaternion [x, y, z, w]");
					}

					pose.Rotation = Animation::Normalize(QuatFromJson(args["rotation"]));
				}
				else if (args.Contains("euler"))
				{
					const Vec3 e = ReadVec(args, "euler", { 0.0f, 0.0f, 0.0f });
					pose.Rotation = EulerRotation(e[0], e[1], e[2]);
				}

				const float fov = args["fov"].AsFloat(pose.Fov);
				pose.Fov = std::isfinite(fov) ? std::clamp(fov, 1.0f, 170.0f) : pose.Fov;
				pose.Camera = "preview";
				pose.Shot.clear();
				pose.Valid = true;
			}

			return pose;
		}

		std::map<std::string, std::string> ReadOptions(const Json& args)
		{
			std::map<std::string, std::string> options;

			for (const auto& [key, value] : args["options"].GetObject())
			{
				options[key] = value.IsString() ? value.AsString() : value.Dump();
			}

			return options;
		}

		std::uint32_t ReadSize(const Json& args, const char* field, std::uint32_t fallback, std::uint32_t max)
		{
			const std::int64_t v = args[field].AsInt(fallback);
			return static_cast<std::uint32_t>(std::clamp<std::int64_t>(v, 16, max));
		}

		std::filesystem::path PreviewFile(PreviewContext& c, const Json& args, const char* stem)
		{
			if (args.Contains("path"))
			{
				std::filesystem::path p(args["path"].AsString());
				return p.is_relative() && !c.Desc.Root.empty() ? c.Desc.Root / p : p;
			}

			std::filesystem::path dir = c.Desc.PreviewDirectory;

			if (dir.empty())
			{
				std::error_code ignored;
				dir = c.Desc.Root.empty() ? std::filesystem::temp_directory_path(ignored) / "replay-previews" : c.Desc.Root / "previews";
			}

			char name[64];
			std::snprintf(name, sizeof(name), "%s-%04llu.png", stem, static_cast<unsigned long long>(++c.Counter));
			return dir / name;
		}

		// Writes the PNG and builds the result (with the image inline when asked).
		Json ImageResult(PreviewContext& c, const Json& args, const char* stem, const Tooling::Image& image)
		{
			const std::vector<std::uint8_t> png = Tooling::EncodePng(image);
			const std::filesystem::path path = PreviewFile(c, args, stem);
			std::error_code ignored;

			if (path.has_parent_path())
			{
				std::filesystem::create_directories(path.parent_path(), ignored);
			}

			if (png.empty() || !Tooling::WritePng(path, image))
			{
				throw ToolError("cannot write " + path.string());
			}

			Json r = Json::MakeObject();
			r["path"] = path.string();
			r["width"] = image.Width;
			r["height"] = image.Height;
			r["bytes"] = static_cast<std::uint64_t>(png.size());

			if (args["inline"].AsBool(true))
			{
				Json content = Json::MakeArray();
				content.Push(Tooling::ImageContent(png));
				r["$content"] = std::move(content);
			}

			return r;
		}

		Json PoseSchemaProperties(Json::Object properties)
		{
			properties["position"] = Schema::Vector(3, "Camera position (default: the evaluated camera at time).");
			properties["rotation"] = Schema::Vector(4, "Camera rotation quaternion [x, y, z, w].");
			properties["euler"] = Schema::Vector(3, "Camera rotation as [yaw, pitch, roll] degrees.");
			properties["lookAt"] = Schema::Vector(3, "Point to look at from position.");
			properties["roll"] = Schema::Number("Roll with lookAt (degrees).");
			properties["fov"] = Schema::Number("Vertical field of view (degrees).");
			return Json(std::move(properties));
		}

		Json ImageProperties(Json::Object properties)
		{
			properties["options"] = Schema::Object();
			properties["options"]["description"] = "Host flags passed through to the renderer (string values; for example "
												   "{ \"hud\": \"off\" }).";
			properties["inline"] = Schema::Boolean("Return the PNG inline as image content (default true).");
			properties["path"] = Schema::String("Output PNG (default: a numbered file in the preview directory).");
			return Json(std::move(properties));
		}

		Json WithSchema(Json properties, std::vector<std::string> required = {})
		{
			Json schema = Schema::Object({}, std::move(required));
			schema["properties"] = std::move(properties);
			return schema;
		}

		void RegisterPreviewTools(Tooling::ToolServer& server, const std::shared_ptr<PreviewContext>& c, const std::string& p)
		{
			server.Register({ p + "preview.render",
				"Renders one frame offscreen: the scene at a timeline time seen from the camera at that time, or from an explicit pose "
				"(position plus rotation, euler or lookAt; fov). Writes a PNG and returns its path (and the image inline unless "
				"inline is false), the pose used and the size.",
				WithSchema(
					[]
					{
						Json::Object props;
						props["time"] = Schema::Number("Timeline seconds (default: the current time).");
						props["width"] = Schema::Integer("Pixels (default 640, at most 4096).");
						props["height"] = Schema::Integer("Pixels (default 360, at most 4096).");
						Json withPose = PoseSchemaProperties(std::move(props));
						return ImageProperties(std::move(withPose.GetObject()));
					}()),
				{}, [c](const Json& args, std::function<void(Json)> reply, std::function<void(std::string)> fail)
				{
					PreviewRequest request;
					request.Time = args.Contains("time") ? args["time"].AsNumber() : c->Player->GetTime();

					if (!std::isfinite(request.Time))
					{
						fail("time must be finite");
						return;
					}

					request.Camera = ReadPose(*c->Player, args, request.Time);
					request.Width = ReadSize(args, "width", 640, 4096);
					request.Height = ReadSize(args, "height", 360, 4096);
					request.Options = ReadOptions(args);
					RenderPreviews(c->Desc.RenderPreview, { request },
						[c, args, request, reply, fail](std::vector<Tooling::Image> images, std::string error)
						{
							if (!error.empty())
							{
								fail("preview failed: " + error);
								return;
							}

							try
							{
								Json r = ImageResult(*c, args, "preview", images.front());
								r["time"] = request.Time;
								r["pose"] = PoseJson(request.Camera);
								reply(std::move(r));
							}
							catch (const std::exception& e)
							{
								fail(e.what());
							}
						});
				} });

			server.Register({ p + "preview.contact_sheet",
				"Renders frames of a shot, a time range or a path and tiles them into one PNG (left to right, top to bottom). shot: "
				"frames spread over the shot with its camera; from/to: the active cameras over that range; path: a camera flying "
				"along the path looking where it goes (at time). Returns the sheet's path (and the image inline unless inline is "
				"false) with each frame's time and pose.",
				WithSchema(
					[]
					{
						Json::Object props;
						props["shot"] = Schema::String("Shot name.");
						props["from"] = Schema::Number("Range start (timeline seconds).");
						props["to"] = Schema::Number("Range end.");
						props["pathName"] = Schema::String("Project path to fly along.");
						props["time"] = Schema::Number("Scene time for a path sheet (default: the current time).");
						props["frames"] = Schema::Integer("Frames (default 9, at most 36).");
						props["columns"] = Schema::Integer("Columns (default: about square).");
						props["cellWidth"] = Schema::Integer("Frame width in pixels (default 320).");
						props["cellHeight"] = Schema::Integer("Frame height in pixels (default 180).");
						props["fov"] = Schema::Number("Field of view for a path sheet (default 60).");
						return ImageProperties(std::move(props));
					}()),
				{}, [c](const Json& args, std::function<void(Json)> reply, std::function<void(std::string)> fail)
				{
					ReplayPlayer& player = *c->Player;
					const auto frames = static_cast<std::uint32_t>(std::clamp<std::int64_t>(args["frames"].AsInt(9), 1, 36));
					const std::uint32_t width = ReadSize(args, "cellWidth", 320, 1024);
					const std::uint32_t height = ReadSize(args, "cellHeight", 180, 1024);
					const auto options = ReadOptions(args);
					std::vector<PreviewRequest> requests;

					const auto request = [&](double time, const CameraPose& pose)
					{
						requests.push_back({ time, pose, width, height, options });
					};

					if (args.Contains("pathName"))
					{
						const auto found = player.GetProject().Paths.find(args["pathName"].AsString());

						if (found == player.GetProject().Paths.end() || found->second.Points.empty())
						{
							fail("no path named '" + args["pathName"].AsString() + "' with points");
							return;
						}

						const double time = args.Contains("time") ? args["time"].AsNumber() : player.GetTime();
						const float fov = std::clamp(args["fov"].AsFloat(60.0f), 1.0f, 170.0f);

						for (std::uint32_t i = 0; i < frames; ++i)
						{
							const float fraction = frames > 1 ? static_cast<float>(i) / static_cast<float>(frames - 1) : 0.0f;
							CameraPose pose;
							pose.Camera = "path:" + args["pathName"].AsString();
							pose.Position = found->second.Sample(fraction);
							pose.Rotation = LookRotation(pose.Position, Animation::Add(pose.Position, found->second.Direction(fraction)));
							pose.Fov = fov;
							pose.Valid = true;
							request(time, pose);
						}
					}
					else
					{
						double from = args["from"].AsNumber(0.0);
						double to = args["to"].AsNumber(player.GetLength());

						if (args.Contains("shot"))
						{
							const Shot* shot = player.GetProject().FindShot(args["shot"].AsString());

							if (!shot)
							{
								fail("no shot named '" + args["shot"].AsString() + "'");
								return;
							}

							from = shot->Start;
							to = shot->End;
						}
						else if (!args.Contains("from") && !args.Contains("to"))
						{
							fail("give shot, from/to or pathName");
							return;
						}

						if (!std::isfinite(from) || !std::isfinite(to) || !(to > from))
						{
							fail("the range is empty");
							return;
						}

						for (const CameraSample& s : SampleCamera(player, from, to, frames))
						{
							request(s.Time, s.Pose);
						}
					}

					const auto columns = static_cast<std::uint32_t>(std::clamp<std::int64_t>(args["columns"].AsInt(0), 0, 36));
					RenderPreviews(c->Desc.RenderPreview, requests,
						[c, args, requests, columns, width, height, reply, fail](std::vector<Tooling::Image> images, std::string error)
						{
							if (!error.empty())
							{
								fail("preview failed: " + error);
								return;
							}

							try
							{
								const Tooling::Image sheet = Tooling::TileImages(images, width, height, columns);
								Json r = ImageResult(*c, args, "sheet", sheet);
								Json list = Json::MakeArray();

								for (const PreviewRequest& q : requests)
								{
									Json f = PoseJson(q.Camera);
									f["time"] = q.Time;
									list.Push(std::move(f));
								}

								r["frames"] = std::move(list);
								reply(std::move(r));
							}
							catch (const std::exception& e)
							{
								fail(e.what());
							}
						});
				} });
		}

		void RegisterVizTools(Tooling::ToolServer& server, const std::shared_ptr<PreviewContext>& c, const std::string& p)
		{
			const auto add = [&](std::string name, std::string description, Json schema, std::function<Json(const Json&)> handler)
			{
				server.Register({ p + name, std::move(description), std::move(schema), std::move(handler) });
			};

			add("viz.path",
				"Drawing data for a project path: the sampled curve, control points, Bezier handles (absolute), ticks at equal "
				"arc-length steps with their directions, and the length.",
				Schema::Object(
					{ { "name", Schema::String("Path name.") }, { "samples", Schema::Integer("Curve samples per segment (default 16).") },
						{ "ticks", Schema::Integer("Arc-length ticks (default 10).") } },
					{ "name" }),
				[c](const Json& args)
				{
					const auto& paths = c->Player->GetProject().Paths;
					const auto found = paths.find(args["name"].AsString());

					if (found == paths.end())
					{
						throw ToolError("no path named '" + args["name"].AsString() + "'");
					}

					const PathViz viz = SamplePathViz(found->second,
						static_cast<std::uint32_t>(std::clamp<std::int64_t>(args["samples"].AsInt(16), 1, 256)),
						static_cast<std::uint32_t>(std::clamp<std::int64_t>(args["ticks"].AsInt(10), 0, 1024)));
					Json r = Json::MakeObject();
					r["name"] = found->first;
					r["length"] = viz.Length;
					r["curve"] = Points(viz.Curve);
					r["keys"] = Points(viz.Keys);
					Json handles = Json::MakeArray();

					for (const auto& h : viz.Handles)
					{
						Json pair = Json::MakeArray();
						pair.Push(ToJson(h[0]));
						pair.Push(ToJson(h[1]));
						handles.Push(std::move(pair));
					}

					r["handles"] = std::move(handles);
					r["ticks"] = Points(viz.Ticks);
					r["tickDirections"] = Points(viz.TickDirections);
					return r;
				});

			add("viz.camera",
				"The camera at a timeline time with its frustum corners (near and far planes: bottom-left, bottom-right, top-right, "
				"top-left) for drawing.",
				Schema::Object({ { "time", Schema::Number("Timeline seconds (default: the current time).") },
					{ "aspect", Schema::Number("Width / height (default 16/9).") },
					{ "near", Schema::Number("Near plane distance (default 0.1).") },
					{ "far", Schema::Number("Far plane distance (default 2).") } }),
				[c](const Json& args)
				{
					const double time = args.Contains("time") ? args["time"].AsNumber() : c->Player->GetTime();
					const CameraPose pose = c->Player->Peek(time).Camera;
					Json r = PoseJson(pose);
					r["time"] = time;
					r["frustum"] = FrustumJson(
						MakeFrustum(pose, args["aspect"].AsFloat(16.0f / 9.0f), args["near"].AsFloat(0.1f), args["far"].AsFloat(2.0f)));
					return r;
				});

			add("viz.shot",
				"The camera's trajectory over a shot or a time range: count samples with times, poses and frusta, plus the positions "
				"as one curve.",
				Schema::Object({ { "shot", Schema::String("Shot name.") }, { "from", Schema::Number("Range start (timeline seconds).") },
					{ "to", Schema::Number("Range end.") }, { "count", Schema::Integer("Samples (default 16, at most 512).") },
					{ "aspect", Schema::Number("Width / height (default 16/9).") },
					{ "far", Schema::Number("Frustum depth (default 1).") } }),
				[c](const Json& args)
				{
					ReplayPlayer& player = *c->Player;
					double from = args["from"].AsNumber(0.0);
					double to = args["to"].AsNumber(player.GetLength());

					if (args.Contains("shot"))
					{
						const Shot* shot = player.GetProject().FindShot(args["shot"].AsString());

						if (!shot)
						{
							throw ToolError("no shot named '" + args["shot"].AsString() + "'");
						}

						from = shot->Start;
						to = shot->End;
					}

					if (!std::isfinite(from) || !std::isfinite(to) || !(to > from))
					{
						throw ToolError("the range is empty");
					}

					const auto count = static_cast<std::uint32_t>(std::clamp<std::int64_t>(args["count"].AsInt(16), 1, 512));
					const float aspect = args["aspect"].AsFloat(16.0f / 9.0f);
					const float depth = args["far"].AsFloat(1.0f);
					Json samples = Json::MakeArray();
					std::vector<Vec3> curve;

					for (const CameraSample& s : SampleCamera(player, from, to, count))
					{
						Json j = PoseJson(s.Pose);
						j["time"] = s.Time;
						j["frustum"] = FrustumJson(MakeFrustum(s.Pose, aspect, 0.0f, depth));
						samples.Push(std::move(j));
						curve.push_back(s.Pose.Position);
					}

					Json r = Json::MakeObject();
					r["from"] = from;
					r["to"] = to;
					r["samples"] = std::move(samples);
					r["curve"] = Points(curve);
					return r;
				});

			add("viz.keys",
				"A director camera's keys in world space for drawing: position keys (or its path and the points its progress keys "
				"reach), look-at keys and field-of-view keys, each with its time.",
				Schema::Object({ { "camera", Schema::String("Rig name.") } }, { "camera" }),
				[c](const Json& args)
				{
					const ReplayProject& project = c->Player->GetProject();
					const auto found = project.Cameras.find(args["camera"].AsString());

					if (found == project.Cameras.end())
					{
						throw ToolError("no camera named '" + args["camera"].AsString() + "'");
					}

					const CameraRig& rig = found->second;
					const auto keys = [](const Track<Vec3>& track)
					{
						Json list = Json::MakeArray();

						for (const auto& k : track.Keys)
						{
							Json j = Json::MakeObject();
							j["time"] = k.Time;
							j["position"] = ToJson(k.Data);
							list.Push(std::move(j));
						}

						return list;
					};

					Json r = Json::MakeObject();
					r["camera"] = rig.Name;
					r["position"] = keys(rig.Position);
					r["lookAt"] = keys(rig.LookAt);

					if (const auto path = project.Paths.find(rig.Path); !rig.Path.empty() && path != project.Paths.end())
					{
						Json progress = Json::MakeArray();

						for (const auto& k : rig.Progress.Keys)
						{
							Json j = Json::MakeObject();
							j["time"] = k.Time;
							j["progress"] = k.Data;
							j["position"] = ToJson(path->second.Sample(k.Data));
							progress.Push(std::move(j));
						}

						r["path"] = rig.Path;
						r["progress"] = std::move(progress);
					}

					Json fov = Json::MakeArray();

					for (const auto& k : rig.Fov.Keys)
					{
						Json j = Json::MakeObject();
						j["time"] = k.Time;
						j["fov"] = k.Data;
						fov.Push(std::move(j));
					}

					r["fov"] = std::move(fov);
					return r;
				});
		}

		// ---- Sequences --------------------------------------------------------------------------

		struct SequenceContext
		{
			SequencePlayer* Sequence = nullptr;
			ReplayToolsDesc Desc;
		};

		std::size_t RequireItem(SequencePlayer& s, const Json& args, const char* field = "index")
		{
			const auto& items = s.GetSequence().Items;

			if (args.Contains("name"))
			{
				const auto found = std::find_if(items.begin(), items.end(),
					[&](const SequenceItem& item)
					{
						return item.Name == args["name"].AsString();
					});

				if (found == items.end())
				{
					throw ToolError("no sequence item named '" + args["name"].AsString() + "'");
				}

				return static_cast<std::size_t>(found - items.begin());
			}

			const std::int64_t index = args[field].AsInt(-1);

			if (index < 0 || static_cast<std::size_t>(index) >= items.size())
			{
				throw ToolError(std::string("give name or a valid '") + field + "' (0.." + std::to_string(items.size()) + ")");
			}

			return static_cast<std::size_t>(index);
		}

		Transition ReadTransition(const Json& args, Transition t)
		{
			if (args.Contains("transition") && !ParseTransitionKind(args["transition"].AsString(), t.Kind))
			{
				throw ToolError("transition must be cut, crossfade or fade");
			}

			const double duration = args["duration"].AsNumber(t.Duration);

			if (!std::isfinite(duration) || duration < 0.0)
			{
				throw ToolError("duration must be zero or more seconds");
			}

			t.Duration = duration;
			t.Color = ReadVec(args, "color", t.Color);
			return t;
		}

		Json SequenceStatus(SequencePlayer& s)
		{
			const SequenceView& view = s.GetView();
			Json r = Json::MakeObject();
			r["time"] = s.GetTime();
			r["length"] = s.GetLength();
			r["playing"] = s.IsPlaying();
			r["primary"] = view.PrimaryIndex;
			r["secondary"] = view.SecondaryIndex;
			r["weight"] = view.Weight;
			r["fadeAmount"] = view.FadeAmount;
			r["fadeColor"] = ToJson(view.FadeColor);
			return r;
		}

		void RegisterSequenceTools(Tooling::ToolServer& server, const std::shared_ptr<SequenceContext>& c, const std::string& p)
		{
			const auto add = [&](std::string name, std::string description, Json schema, std::function<Json(const Json&)> handler)
			{
				server.Register({ p + name, std::move(description), std::move(schema), std::move(handler) });
			};

			const auto resolve = [c](const std::string& path)
			{
				const std::filesystem::path f(path);
				return f.is_relative() && !c->Desc.Root.empty() ? c->Desc.Root / f : f;
			};

			const Json transitionSchema = Schema::Enum({ "cut", "crossfade", "fade" }, "Transition from the previous item.");

			add("sequence.add",
				"Adds a recording to the multi-replay sequence (at index, default the end), played from in to out of its timeline "
				"with its project, entered by a transition (cut, crossfade overlapping by duration, or fade through color).",
				Schema::Object(
					{ { "recording", Schema::String("Recording file.") }, { "project", Schema::String("Optional project file.") },
						{ "name", Schema::String("Item name (default: the recording's file name).") },
						{ "in", Schema::Number("Item timeline start (default 0).") },
						{ "out", Schema::Number("Item timeline end (default: its end).") },
						{ "index", Schema::Integer("Insert position (default: the end).") }, { "transition", transitionSchema },
						{ "duration", Schema::Number("Transition seconds.") },
						{ "color", Schema::Vector(3, "Fade colour (linear RGB, default black).") } },
					{ "recording" }),
				[c](const Json& args)
				{
					SequencePlayer& s = *c->Sequence;
					SequenceItem item;
					item.Recording = args["recording"].AsString();

					if (item.Recording.empty())
					{
						throw ToolError("recording is empty");
					}

					item.Project = args["project"].AsString();
					item.Name = args["name"].AsString(std::filesystem::path(item.Recording).stem().string());
					item.In = std::max(args["in"].AsNumber(0.0), 0.0);
					item.Out = args["out"].AsNumber(-1.0);

					if (!std::isfinite(item.In) || !std::isfinite(item.Out))
					{
						throw ToolError("in and out must be finite");
					}

					item.Enter = ReadTransition(args, {});
					auto& items = s.GetSequence().Items;
					const auto index = static_cast<std::size_t>(std::clamp<std::int64_t>(
						args["index"].AsInt(static_cast<std::int64_t>(items.size())), 0, static_cast<std::int64_t>(items.size())));
					items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), item);
					s.Changed();
					ReplayPlayer* player = s.GetPlayer(index);
					Json r = SequenceStatus(s);
					r["index"] = static_cast<std::uint64_t>(index);
					r["opened"] = player != nullptr;

					if (!player)
					{
						r["error"] = s.GetError(index);
					}

					return r;
				});

			add("sequence.remove", "Removes a sequence item by index or name.",
				Schema::Object({ { "index", Schema::Integer("Item index.") }, { "name", Schema::String("Item name.") } }),
				[c](const Json& args)
				{
					SequencePlayer& s = *c->Sequence;
					const std::size_t index = RequireItem(s, args);
					auto& items = s.GetSequence().Items;
					items.erase(items.begin() + static_cast<std::ptrdiff_t>(index));
					s.Changed();
					s.Seek(s.GetTime());
					return SequenceStatus(s);
				});

			add("sequence.move", "Moves a sequence item from one index to another.",
				Schema::Object(
					{ { "from", Schema::Integer("Current index.") }, { "to", Schema::Integer("New index.") } }, { "from", "to" }),
				[c](const Json& args)
				{
					SequencePlayer& s = *c->Sequence;
					const std::size_t from = RequireItem(s, args, "from");
					auto& items = s.GetSequence().Items;
					const auto to = static_cast<std::size_t>(
						std::clamp<std::int64_t>(args["to"].AsInt(0), 0, static_cast<std::int64_t>(items.size()) - 1));
					SequenceItem item = std::move(items[from]);
					items.erase(items.begin() + static_cast<std::ptrdiff_t>(from));
					items.insert(items.begin() + static_cast<std::ptrdiff_t>(to), std::move(item));
					s.Changed();
					s.Seek(s.GetTime());
					return SequenceStatus(s);
				});

			add("sequence.set_transition",
				"Sets how an item is entered from the previous one: cut, crossfade (overlap) or fade (through color).",
				Schema::Object({ { "index", Schema::Integer("Item index.") }, { "name", Schema::String("Item name.") },
					{ "transition", transitionSchema }, { "duration", Schema::Number("Seconds.") },
					{ "color", Schema::Vector(3, "Fade colour (linear RGB).") }, { "in", Schema::Number("New item timeline start.") },
					{ "out", Schema::Number("New item timeline end.") } }),
				[c](const Json& args)
				{
					SequencePlayer& s = *c->Sequence;
					SequenceItem& item = s.GetSequence().Items[RequireItem(s, args)];
					item.Enter = ReadTransition(args, item.Enter);
					item.In = std::max(args["in"].AsNumber(item.In), 0.0);
					item.Out = args["out"].AsNumber(item.Out);
					s.Changed();
					s.Seek(s.GetTime());
					return SequenceStatus(s);
				});

			add("sequence.list", "The sequence items with their placement on the sequence timeline and any open errors.", Schema::Object(),
				[c](const Json&)
				{
					SequencePlayer& s = *c->Sequence;
					const auto& spans = s.GetSpans();
					Json list = Json::MakeArray();
					const auto& items = s.GetSequence().Items;

					for (std::size_t i = 0; i < items.size(); ++i)
					{
						Json j = ReplaySequence{ { items[i] } }.ToJson()["items"][std::size_t{ 0 }];
						j["index"] = static_cast<std::uint64_t>(i);

						if (i < spans.size())
						{
							j["start"] = spans[i].Start;
							j["end"] = spans[i].End;
						}

						if (!s.GetError(i).empty())
						{
							j["error"] = s.GetError(i);
						}

						list.Push(std::move(j));
					}

					Json r = SequenceStatus(s);
					r["items"] = std::move(list);
					return r;
				});

			add("sequence.seek", "Moves the sequence to a time (sequence seconds).",
				Schema::Object({ { "time", Schema::Number("Sequence seconds.") } }, { "time" }),
				[c](const Json& args)
				{
					const double t = args["time"].AsNumber();

					if (!std::isfinite(t))
					{
						throw ToolError("time must be finite");
					}

					c->Sequence->Seek(t);
					return SequenceStatus(*c->Sequence);
				});

			add("sequence.play", "Plays the sequence.", Schema::Object(),
				[c](const Json&)
				{
					c->Sequence->Play();
					return SequenceStatus(*c->Sequence);
				});

			add("sequence.pause", "Pauses the sequence.", Schema::Object(),
				[c](const Json&)
				{
					c->Sequence->Pause();
					return SequenceStatus(*c->Sequence);
				});

			add("sequence.status", "Sequence time, length, the playing items and the transition state (crossfade weight, fade).",
				Schema::Object(),
				[c](const Json&)
				{
					return SequenceStatus(*c->Sequence);
				});

			add("sequence.save", "Writes the sequence as JSON.",
				Schema::Object({ { "path", Schema::String("File (default: the host's sequence path).") } }),
				[c, resolve](const Json& args)
				{
					const std::filesystem::path path = args.Contains("path") ? resolve(args["path"].AsString()) : c->Desc.SequencePath;

					if (path.empty())
					{
						throw ToolError("no path given and the host set no sequence path");
					}

					if (!c->Sequence->GetSequence().Save(path))
					{
						throw ToolError("cannot write " + path.string());
					}

					Json r = Json::MakeObject();
					r["path"] = path.string();
					return r;
				});

			add("sequence.load", "Loads a sequence JSON file (replacing the current one).",
				Schema::Object({ { "path", Schema::String("Sequence file.") } }, { "path" }),
				[c, resolve](const Json& args)
				{
					std::string error;
					ReplaySequence loaded;

					if (!loaded.Load(resolve(args["path"].AsString()), &error))
					{
						throw ToolError("cannot load sequence: " + error);
					}

					c->Sequence->GetSequence() = std::move(loaded);
					c->Sequence->Changed();
					c->Sequence->Seek(0.0);
					return SequenceStatus(*c->Sequence);
				});
		}

	} // namespace

	namespace Internal
	{

		void RegisterStudioTools(Tooling::ToolServer& server, ReplayPlayer& player, const ReplayToolsDesc& desc)
		{
			auto preview = std::make_shared<PreviewContext>();
			preview->Player = &player;
			preview->Desc = desc;
			RegisterPreviewTools(server, preview, desc.Prefix);
			RegisterVizTools(server, preview, desc.Prefix);

			if (desc.Sequence)
			{
				auto sequence = std::make_shared<SequenceContext>();
				sequence->Sequence = desc.Sequence;
				sequence->Desc = desc;
				RegisterSequenceTools(server, sequence, desc.Prefix);
			}
		}

	} // namespace Internal

} // namespace Swim::Replay
