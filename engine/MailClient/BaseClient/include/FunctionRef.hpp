#ifndef AURORA_MAIL_FUNCTION_REF_HPP
#define AURORA_MAIL_FUNCTION_REF_HPP

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

namespace aurora::mail::common
{
  /**
   * @brief Non-owning, type-erased reference to a callable.
   *
   * A two-pointer view of any invocable object — like @c std::function but
   * without heap allocation, copies, or capture-state ownership. The
   * @c FunctionRef stores a pointer to the original callable and a thunk
   * that re-types the call back to it.
   *
   * Lifetime contract: the referenced callable must outlive every call made
   * through the reference. This is true by construction whenever a
   * @c FunctionRef is taken as a function parameter and the caller passed a
   * temporary lambda — the temporary lives until the end of the full
   * expression in the caller's frame, and a coroutine that takes a
   * @c FunctionRef parameter copies the two pointers into its own frame, so
   * lifetime is bounded by the originating @c co_await expression. Functions
   * (passed by name) live in the binary, so their address is permanent.
   *
   * Not designed to be stored across suspension boundaries that outlive the
   * caller's full expression. For long-lived storage, copy the underlying
   * callable into a @c std::function.
   *
   * Implementation note on function vs. object dispatch: C++ does not
   * permit @c static_cast between function pointers and @c void*, so the
   * function-type branch uses @c reinterpret_cast — which is conditionally
   * supported but works on every platform Aurora targets (POSIX + Windows).
   * Object pointers use the strictly portable @c static_cast path.
   */
  template<typename Sig>
  class FunctionRef;

  template<typename R, typename... Args>
  class FunctionRef<R(Args...)>
  {
   public:
    FunctionRef() noexcept = default;

    template<typename F>
      requires(!std::same_as<std::remove_cvref_t<F>, FunctionRef>) && std::invocable<F&, Args...> &&
              (std::same_as<R, void> || std::convertible_to<std::invoke_result_t<F&, Args...>, R>)
    FunctionRef(F&& f) noexcept
    {
      using Decayed = std::remove_reference_t<F>;
      if constexpr (std::is_function_v<Decayed>)
      {
        // F is a function type (e.g. when the caller wrote `isSmtpFinalLine`
        // and that name is a function lvalue, not a function pointer). The
        // function lives at a permanent address in the binary; encode its
        // pointer into the void* slot via reinterpret_cast.
        object_ = reinterpret_cast<void*>(static_cast<Decayed*>(std::addressof(f)));
        invoke_ = &invokeFunctionImpl<Decayed>;
      }
      else
      {
        // F is an object type (lambda, functor, function-pointer object,
        // std::function, ...). Store its address; standard static_cast path.
        object_ = const_cast<void*>(static_cast<const void*>(std::addressof(f)));
        invoke_ = &invokeObjectImpl<Decayed>;
      }
    }

    R operator()(Args... args) const
    {
      return invoke_(object_, std::forward<Args>(args)...);
    }

    explicit operator bool() const noexcept
    {
      return invoke_ != nullptr;
    }

   private:
    template<typename Obj>
    static R invokeObjectImpl(void* obj, Args... args)
    {
      auto& f = *static_cast<Obj*>(obj);
      if constexpr (std::same_as<R, void>)
      {
        static_cast<void>(f(std::forward<Args>(args)...));
      }
      else
      {
        return f(std::forward<Args>(args)...);
      }
    }

    template<typename Func>
    static R invokeFunctionImpl(void* obj, Args... args)
    {
      auto* fp = reinterpret_cast<Func*>(obj);
      if constexpr (std::same_as<R, void>)
      {
        static_cast<void>((*fp)(std::forward<Args>(args)...));
      }
      else
      {
        return (*fp)(std::forward<Args>(args)...);
      }
    }

    void* object_ = nullptr;
    R (*invoke_)(void*, Args...) = nullptr;
  };

}  // namespace aurora::mail::common

#endif  // AURORA_MAIL_FUNCTION_REF_HPP
