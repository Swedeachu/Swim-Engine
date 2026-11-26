#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Swim::Tooling
{

	// A small JSON document model for tool messages and editable project files.
	// Objects keep their keys sorted, so written files are stable and diff well.
	class Json
	{

	  public:
		using Array = std::vector<Json>;
		using Object = std::map<std::string, Json, std::less<>>;

		Json() = default;

		Json(std::nullptr_t) {}

		Json(bool value) : value(value) {}

		Json(int value) : value(static_cast<double>(value)) {}

		Json(std::int64_t value) : value(static_cast<double>(value)) {}

		Json(std::uint64_t value) : value(static_cast<double>(value)) {}

		Json(std::uint32_t value) : value(static_cast<double>(value)) {}

		Json(float value) : value(static_cast<double>(value)) {}

		Json(double value) : value(value) {}

		Json(const char* value) : value(std::string(value)) {}

		Json(std::string value) : value(std::move(value)) {}

		Json(std::string_view value) : value(std::string(value)) {}

		Json(Array value) : value(std::make_shared<Array>(std::move(value))) {}

		Json(Object value) : value(std::make_shared<Object>(std::move(value))) {}

		static Json MakeArray() { return Json(Array{}); }

		static Json MakeObject() { return Json(Object{}); }

		// Throws std::runtime_error with the byte offset on malformed input.
		static Json Parse(std::string_view text);

		static std::optional<Json> TryParse(std::string_view text, std::string* error = nullptr);

		// indent < 0: one line (tool messages); otherwise pretty-printed with that many spaces.
		std::string Dump(int indent = -1) const;

		bool IsNull() const { return std::holds_alternative<std::monostate>(value); }

		bool IsBool() const { return std::holds_alternative<bool>(value); }

		bool IsNumber() const { return std::holds_alternative<double>(value); }

		bool IsString() const { return std::holds_alternative<std::string>(value); }

		bool IsArray() const { return std::holds_alternative<std::shared_ptr<Array>>(value); }

		bool IsObject() const { return std::holds_alternative<std::shared_ptr<Object>>(value); }

		bool AsBool(bool fallback = false) const;

		double AsNumber(double fallback = 0.0) const;

		float AsFloat(float fallback = 0.0f) const { return static_cast<float>(AsNumber(fallback)); }

		std::int64_t AsInt(std::int64_t fallback = 0) const;

		const std::string& AsString() const; // Empty when not a string.

		std::string AsString(std::string_view fallback) const;

		// Converts null to an empty array/object first; throws on other types.
		Array& GetArray();

		const Array& GetArray() const; // Empty when not an array.

		Object& GetObject();

		const Object& GetObject() const; // Empty when not an object.

		std::size_t Size() const;

		// Object access. The const form returns a shared null for missing keys.
		Json& operator[](std::string_view key);

		const Json& operator[](std::string_view key) const;

		const Json* Find(std::string_view key) const;

		bool Contains(std::string_view key) const { return Find(key) != nullptr; }

		// Array access.
		const Json& operator[](std::size_t index) const;

		Json& Push(Json item);

		bool operator==(const Json& other) const;

	  private:
		// Containers are shared on copy and cloned before mutation (copy on write), so
		// passing documents around by value stays cheap.
		void Detach();

		std::variant<std::monostate, bool, double, std::string, std::shared_ptr<Array>, std::shared_ptr<Object>> value;
	};

} // namespace Swim::Tooling
