#pragma once
#include "Engine/Machine.h"

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <type_traits>
#include <memory>
#include <tuple>

namespace Engine
{
  
  // Most commands will be sent externally via IPC 
  class CommandSystem : public Machine
  {

  public:

    int Awake() override;
    int Init() override;
    void Update(double /*dt*/) override {}
    void FixedUpdate(unsigned int /*tickThisSecond*/) override {}
    int Exit() override;

    // Parse a message like:  "(spawn 10 20 "Enemy Grunt")"
    // Returns true if a known command ran successfully.
    bool ParseAndDispatch(const std::string& message);

    // Dispatch a command that already has split args
    bool Dispatch(const std::string& commandName, const std::vector<std::string>& args);

    // Register a raw command that receives tokens verbatim
    void RegisterRaw(const std::string& commandName, std::function<void(const std::vector<std::string>&)> fn);

    // Register a strongly-typed command using std::function<void(Args...)>
    template<typename... Args>
    void Register(const std::string& commandName, std::function<void(Args...)> fn)
    {
      auto wrapper = MakeTypedWrapper<Args...>(std::move(fn));
      RegisterImpl(commandName, std::move(wrapper));
    }

    // Register arbitrary callable (lambda, function pointer, std::function, functor)
    template<typename F>
    void Register(const std::string& commandName, F&& f)
    {
      using Traits = function_traits<std::decay_t<F>>;
      static_assert(std::is_same_v<typename Traits::return_type, void>, "Command callable must return void");

      Register<typename Traits::args_tuple_elements...>(
        commandName,
        std::function<void(typename Traits::args_tuple_elements...)>(std::forward<F>(f))
      );
    }

    // Register a member function with an instance pointer
    template<typename C, typename R, typename... A>
    void Register(const std::string& commandName, C* instance, R(C::* method)(A...))
    {
      static_assert(std::is_same_v<R, void>, "Command member function must return void");
      auto f = [instance, method](A... args) { (instance->*method)(std::forward<A>(args)...); };
      Register(commandName, std::function<void(A...)>(std::move(f)));
    }

    // Register a const-qualified member function with an instance pointer
    template<typename C, typename R, typename... A>
    void Register(const std::string& commandName, const C* instance, R(C::* method)(A...) const)
    {
      static_assert(std::is_same_v<R, void>, "Command member function must return void");
      auto f = [instance, method](A... args) { (instance->*method)(std::forward<A>(args)...); };
      Register(commandName, std::function<void(A...)>(std::move(f)));
    }

  private:

    struct ICmd
    {
      virtual ~ICmd() = default;
      virtual bool Call(const std::vector<std::string>& args) = 0;
    };

    struct RawCmd : ICmd
    {
      std::function<void(const std::vector<std::string>&)> fn;
      bool Call(const std::vector<std::string>& args) override
      {
        fn(args);
        return true;
      }
    };

    template<typename... Args>
    struct TypedCmd final : ICmd
    {
      std::function<void(Args...)> fn;
      bool Call(const std::vector<std::string>& args) override
      {
        if (args.size() != sizeof...(Args)) return false;
        return InvokeWithConvertedArgs(std::index_sequence_for<Args...>{}, args);
      }

      template<std::size_t... I>
      bool InvokeWithConvertedArgs(std::index_sequence<I...>, const std::vector<std::string>& args)
      {
        std::tuple<std::decay_t<Args>...> tup;

        if (!(ConvertArg<std::tuple_element_t<I, decltype(tup)>>(args[I], std::get<I>(tup)) && ...))
        {
          return false;
        }

        std::apply(fn, tup);

        return true;
      }
    };

    void RegisterImpl(const std::string& commandName, std::unique_ptr<ICmd> cmd);

    template<typename... Args>
    std::unique_ptr<ICmd> MakeTypedWrapper(std::function<void(Args...)> fn)
    {
      auto c = std::make_unique<TypedCmd<Args...>>();
      c->fn = std::move(fn);
      return c;
    }

    static bool SplitTokens(const std::string& line, std::vector<std::string>& outTokens);

    // Conversions from std::string to primitive types. Returns bool for success or fail.
    static bool ConvertArg(const std::string& s, std::string& out);
    static bool ConvertArg(const std::string& s, bool& out);
    static bool ConvertArg(const std::string& s, int& out);
    static bool ConvertArg(const std::string& s, unsigned& out);
    static bool ConvertArg(const std::string& s, long& out);
    static bool ConvertArg(const std::string& s, unsigned long& out);
    static bool ConvertArg(const std::string& s, long long& out);
    static bool ConvertArg(const std::string& s, unsigned long long& out);
    static bool ConvertArg(const std::string& s, float& out);
    static bool ConvertArg(const std::string& s, double& out);

    template<typename E>
    static std::enable_if_t<std::is_enum_v<E>, bool>ConvertArg(const std::string& s, E& out)
    {
      using U = std::underlying_type_t<E>;
      U tmp{};

      if (!ConvertArg(s, tmp))
      {
        return false;
      }

      out = static_cast<E>(tmp);

      return true;
    }

    template<typename T>
    static std::enable_if_t<!std::is_enum_v<T> &&
      !std::is_same_v<T, std::string> &&
      !std::is_same_v<T, bool> &&
      !std::is_integral_v<T> &&
      !std::is_floating_point_v<T>, bool>
      ConvertArg(const std::string& /*s*/, T& /*out*/)
    {
      static_assert(sizeof(T) == 0, "No converter for this argument type. Provide an overload.");
      return false;
    }

    template<typename T>
    struct function_traits;

    // function type
    template<typename R, typename... A>
    struct function_traits<R(A...)>
    {
      using return_type = R;
      using args_tuple = std::tuple<A...>;
      template<typename... T> struct pack { using type = pack; };
      using args_tuple_elements_pack = pack<A...>;
      // Helper alias to unpack in Register<>
      template<typename... T> struct identity { using type = void; };
      using args_tuple_elements = std::tuple_element_t<0, std::tuple<std::conditional_t<true, A, void>...>>; // unused placeholder
    };

    // function pointer
    template<typename R, typename... A>
    struct function_traits<R(*)(A...)> : function_traits<R(A...)> {};

    // std::function
    template<typename R, typename... A>
    struct function_traits<std::function<R(A...)>> : function_traits<R(A...)> {};

    // member function pointer
    template<typename C, typename R, typename... A>
    struct function_traits<R(C::*)(A...)> : function_traits<R(A...)> {};

    // const member function pointer
    template<typename C, typename R, typename... A>
    struct function_traits<R(C::*)(A...) const> : function_traits<R(A...)> {};

    // functor/lambda: deduce from operator()
    template<typename F>
    struct function_traits : function_traits<decltype(&F::operator())> {};

    // Small helper to unpack args in Register<F>(...)
    template<typename F>
    struct traits_unpack;

    template<typename R, typename... A>
    struct traits_unpack<R(A...)>
    {
      using return_type = R;
      using args_tuple = std::tuple<A...>;
    };

    template<typename F>
    using traits_return_t = typename function_traits<F>::return_type;

    // This alias pulls out the variadic pack from function_traits into a real parameter pack
    template<typename F>
    using traits_args_tuple_t = typename traits_unpack<typename function_traits<F>::template function_type>::args_tuple;

    std::unordered_map<std::string, std::unique_ptr<ICmd>> commandRegistry;

  };

} // namespace Engine
