#include "Engine/Systems/Tooling/Json.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace Swim::Tooling
{

	namespace
	{

		const Json& NullJson()
		{
			static const Json null;
			return null;
		}

		class Parser
		{

		  public:
			explicit Parser(std::string_view text) : text(text) {}

			Json ParseDocument()
			{
				Json result = ParseValue(0);
				SkipSpace();

				if (at != text.size())
				{
					Fail("trailing characters");
				}

				return result;
			}

		  private:
			[[noreturn]] void Fail(const char* what) const
			{
				throw std::runtime_error(std::string("JSON: ") + what + " at byte " + std::to_string(at));
			}

			void SkipSpace()
			{
				while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r'))
				{
					++at;
				}
			}

			bool Consume(std::string_view word)
			{
				if (text.substr(at, word.size()) == word)
				{
					at += word.size();
					return true;
				}

				return false;
			}

			Json ParseValue(int depth)
			{
				if (depth > 256)
				{
					Fail("nesting too deep");
				}

				SkipSpace();

				if (at >= text.size())
				{
					Fail("unexpected end");
				}

				const char c = text[at];

				if (c == '{')
				{
					++at;
					Json::Object object;
					SkipSpace();

					if (at < text.size() && text[at] == '}')
					{
						++at;
						return Json(std::move(object));
					}

					while (true)
					{
						SkipSpace();

						if (at >= text.size() || text[at] != '"')
						{
							Fail("expected a key");
						}

						std::string key = ParseString();
						SkipSpace();

						if (at >= text.size() || text[at] != ':')
						{
							Fail("expected ':'");
						}

						++at;
						object[std::move(key)] = ParseValue(depth + 1);
						SkipSpace();

						if (at < text.size() && text[at] == ',')
						{
							++at;
							continue;
						}

						if (at < text.size() && text[at] == '}')
						{
							++at;
							return Json(std::move(object));
						}

						Fail("expected ',' or '}'");
					}
				}

				if (c == '[')
				{
					++at;
					Json::Array array;
					SkipSpace();

					if (at < text.size() && text[at] == ']')
					{
						++at;
						return Json(std::move(array));
					}

					while (true)
					{
						array.push_back(ParseValue(depth + 1));
						SkipSpace();

						if (at < text.size() && text[at] == ',')
						{
							++at;
							continue;
						}

						if (at < text.size() && text[at] == ']')
						{
							++at;
							return Json(std::move(array));
						}

						Fail("expected ',' or ']'");
					}
				}

				if (c == '"')
				{
					return Json(ParseString());
				}

				if (Consume("true"))
				{
					return Json(true);
				}

				if (Consume("false"))
				{
					return Json(false);
				}

				if (Consume("null"))
				{
					return Json();
				}

				return Json(ParseNumber());
			}

			double ParseNumber()
			{
				const std::size_t start = at;

				while (at < text.size() &&
					(std::isdigit(static_cast<unsigned char>(text[at])) || text[at] == '-' || text[at] == '+' || text[at] == '.' ||
						text[at] == 'e' || text[at] == 'E'))
				{
					++at;
				}

				double value = 0.0;
				const auto result = std::from_chars(text.data() + start, text.data() + at, value);

				if (start == at || result.ec != std::errc() || result.ptr != text.data() + at)
				{
					at = start;
					Fail("bad value");
				}

				return value;
			}

			std::uint32_t ParseHex4()
			{
				if (at + 4 > text.size())
				{
					Fail("short unicode escape");
				}

				std::uint32_t code = 0;

				for (int i = 0; i < 4; ++i)
				{
					const char h = text[at++];
					code <<= 4;

					if (h >= '0' && h <= '9')
					{
						code |= static_cast<std::uint32_t>(h - '0');
					}
					else if (h >= 'a' && h <= 'f')
					{
						code |= static_cast<std::uint32_t>(h - 'a' + 10);
					}
					else if (h >= 'A' && h <= 'F')
					{
						code |= static_cast<std::uint32_t>(h - 'A' + 10);
					}
					else
					{
						Fail("bad unicode escape");
					}
				}

				return code;
			}

			static void AppendUtf8(std::string& out, std::uint32_t code)
			{
				if (code < 0x80)
				{
					out += static_cast<char>(code);
				}
				else if (code < 0x800)
				{
					out += static_cast<char>(0xC0 | (code >> 6));
					out += static_cast<char>(0x80 | (code & 0x3F));
				}
				else if (code < 0x10000)
				{
					out += static_cast<char>(0xE0 | (code >> 12));
					out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
					out += static_cast<char>(0x80 | (code & 0x3F));
				}
				else
				{
					out += static_cast<char>(0xF0 | (code >> 18));
					out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
					out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
					out += static_cast<char>(0x80 | (code & 0x3F));
				}
			}

			std::string ParseString()
			{
				++at; // Opening quote.
				std::string out;

				while (true)
				{
					if (at >= text.size())
					{
						Fail("unterminated string");
					}

					const char c = text[at++];

					if (c == '"')
					{
						return out;
					}

					if (c != '\\')
					{
						out += c;
						continue;
					}

					if (at >= text.size())
					{
						Fail("unterminated escape");
					}

					const char e = text[at++];

					switch (e)
					{
					case '"':
					case '\\':
					case '/':
						out += e;
						break;
					case 'b':
						out += '\b';
						break;
					case 'f':
						out += '\f';
						break;
					case 'n':
						out += '\n';
						break;
					case 'r':
						out += '\r';
						break;
					case 't':
						out += '\t';
						break;
					case 'u':
					{
						std::uint32_t code = ParseHex4();

						if (code >= 0xD800 && code <= 0xDBFF && text.substr(at, 2) == "\\u")
						{
							at += 2;
							const std::uint32_t low = ParseHex4();
							code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
						}

						AppendUtf8(out, code);
						break;
					}
					default:
						Fail("bad escape");
					}
				}
			}

			std::string_view text;
			std::size_t at = 0;
		};

		void WriteString(std::string& out, const std::string& s)
		{
			out += '"';

			for (const char c : s)
			{
				switch (c)
				{
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					if (static_cast<unsigned char>(c) < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
						out += buffer;
					}
					else
					{
						out += c;
					}
				}
			}

			out += '"';
		}

		void WriteNumber(std::string& out, double v)
		{
			if (!std::isfinite(v))
			{
				out += "null";
				return;
			}

			if (v == std::floor(v) && std::abs(v) < 9.0e15)
			{
				out += std::to_string(static_cast<std::int64_t>(v));
				return;
			}

			char buffer[32];
			const auto result = std::to_chars(buffer, buffer + sizeof(buffer), v);
			out.append(buffer, result.ptr);
		}

		void Newline(std::string& out, int indent, int depth)
		{
			if (indent >= 0)
			{
				out += '\n';
				out.append(static_cast<std::size_t>(indent * depth), ' ');
			}
		}

	} // namespace

	Json Json::Parse(std::string_view text)
	{
		return Parser(text).ParseDocument();
	}

	std::optional<Json> Json::TryParse(std::string_view text, std::string* error)
	{
		try
		{
			return Parse(text);
		}
		catch (const std::exception& e)
		{
			if (error)
			{
				*error = e.what();
			}

			return std::nullopt;
		}
	}

	namespace
	{

		void DumpInto(std::string& out, const Json& j, int indent, int depth)
		{
			if (j.IsNull())
			{
				out += "null";
			}
			else if (j.IsBool())
			{
				out += j.AsBool() ? "true" : "false";
			}
			else if (j.IsNumber())
			{
				WriteNumber(out, j.AsNumber());
			}
			else if (j.IsString())
			{
				WriteString(out, j.AsString());
			}
			else if (j.IsArray())
			{
				const auto& array = j.GetArray();
				out += '[';

				for (std::size_t i = 0; i < array.size(); ++i)
				{
					if (i)
					{
						out += ',';
					}

					Newline(out, indent, depth + 1);
					DumpInto(out, array[i], indent, depth + 1);
				}

				if (!array.empty())
				{
					Newline(out, indent, depth);
				}

				out += ']';
			}
			else
			{
				const auto& object = j.GetObject();
				out += '{';
				bool first = true;

				for (const auto& [key, item] : object)
				{
					if (!first)
					{
						out += ',';
					}

					first = false;
					Newline(out, indent, depth + 1);
					WriteString(out, key);
					out += indent >= 0 ? ": " : ":";
					DumpInto(out, item, indent, depth + 1);
				}

				if (!object.empty())
				{
					Newline(out, indent, depth);
				}

				out += '}';
			}
		}

	} // namespace

	std::string Json::Dump(int indent) const
	{
		std::string out;
		DumpInto(out, *this, indent, 0);
		return out;
	}

	bool Json::AsBool(bool fallback) const
	{
		if (const auto* b = std::get_if<bool>(&value))
		{
			return *b;
		}

		if (const auto* d = std::get_if<double>(&value))
		{
			return *d != 0.0;
		}

		return fallback;
	}

	double Json::AsNumber(double fallback) const
	{
		if (const auto* d = std::get_if<double>(&value))
		{
			return *d;
		}

		if (const auto* b = std::get_if<bool>(&value))
		{
			return *b ? 1.0 : 0.0;
		}

		return fallback;
	}

	std::int64_t Json::AsInt(std::int64_t fallback) const
	{
		if (const auto* d = std::get_if<double>(&value))
		{
			return static_cast<std::int64_t>(*d);
		}

		return fallback;
	}

	const std::string& Json::AsString() const
	{
		static const std::string empty;
		const auto* s = std::get_if<std::string>(&value);
		return s ? *s : empty;
	}

	std::string Json::AsString(std::string_view fallback) const
	{
		const auto* s = std::get_if<std::string>(&value);
		return s ? *s : std::string(fallback);
	}

	void Json::Detach()
	{
		if (auto* a = std::get_if<std::shared_ptr<Array>>(&value); a && a->use_count() > 1)
		{
			*a = std::make_shared<Array>(**a);
		}
		else if (auto* o = std::get_if<std::shared_ptr<Object>>(&value); o && o->use_count() > 1)
		{
			*o = std::make_shared<Object>(**o);
		}
	}

	Json::Array& Json::GetArray()
	{
		if (IsNull())
		{
			value = std::make_shared<Array>();
		}

		if (!IsArray())
		{
			throw std::runtime_error("JSON value is not an array");
		}

		Detach();
		return *std::get<std::shared_ptr<Array>>(value);
	}

	const Json::Array& Json::GetArray() const
	{
		static const Array empty;
		const auto* a = std::get_if<std::shared_ptr<Array>>(&value);
		return a ? **a : empty;
	}

	Json::Object& Json::GetObject()
	{
		if (IsNull())
		{
			value = std::make_shared<Object>();
		}

		if (!IsObject())
		{
			throw std::runtime_error("JSON value is not an object");
		}

		Detach();
		return *std::get<std::shared_ptr<Object>>(value);
	}

	const Json::Object& Json::GetObject() const
	{
		static const Object empty;
		const auto* o = std::get_if<std::shared_ptr<Object>>(&value);
		return o ? **o : empty;
	}

	std::size_t Json::Size() const
	{
		return IsArray() ? GetArray().size() : IsObject() ? GetObject().size() : 0;
	}

	Json& Json::operator[](std::string_view key)
	{
		auto& object = GetObject();
		const auto found = object.find(key);

		if (found != object.end())
		{
			return found->second;
		}

		return object.emplace(std::string(key), Json()).first->second;
	}

	const Json& Json::operator[](std::string_view key) const
	{
		const Json* found = Find(key);
		return found ? *found : NullJson();
	}

	const Json* Json::Find(std::string_view key) const
	{
		const auto& object = GetObject();
		const auto found = object.find(key);
		return found == object.end() ? nullptr : &found->second;
	}

	const Json& Json::operator[](std::size_t index) const
	{
		const auto& array = GetArray();
		return index < array.size() ? array[index] : NullJson();
	}

	Json& Json::Push(Json item)
	{
		auto& array = GetArray();
		array.push_back(std::move(item));
		return array.back();
	}

	bool Json::operator==(const Json& other) const
	{
		if (value.index() != other.value.index())
		{
			return false;
		}

		if (IsArray())
		{
			return GetArray() == other.GetArray();
		}

		if (IsObject())
		{
			return GetObject() == other.GetObject();
		}

		return value == other.value;
	}

} // namespace Swim::Tooling
