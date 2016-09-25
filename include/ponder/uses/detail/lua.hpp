/****************************************************************************
**
** This file is part of the Ponder library.
**
** The MIT License (MIT)
**
** Copyright (C) 2016 Bill Quith.
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
** 
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
** THE SOFTWARE.
**
****************************************************************************/

#ifndef PONDER_USES_DETAIL_LUA_HPP
#define PONDER_USES_DETAIL_LUA_HPP

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace ponder {
namespace lua {
    
// forward declare
int pushUserObject(lua_State *L, const UserObject& uobj);

namespace impl {

//-----------------------------------------------------------------------------
// Write values to Lua. Push to stack.

template <typename T, typename U = void> struct LuaValueWriter {};

template <typename T>
struct LuaValueWriter<T, typename std::enable_if<std::is_integral<T>::value>::type>
{
    static inline int push(lua_State *L, T value)
    {
        return lua_pushinteger(L, value), 1;
    }
};

template <typename T>
struct LuaValueWriter<T, typename std::enable_if<std::is_floating_point<T>::value>::type>
{
    static inline int push(lua_State *L, T value)
    {
        return lua_pushnumber(L, value), 1;
    }
};

template <>
struct LuaValueWriter<std::string>
{
    static inline int push(lua_State *L, const std::string& value)
    {
        return lua_pushstring(L, value.data()), 1;
    }
};

template <>
struct LuaValueWriter<detail::string_view>
{
    static inline int push(lua_State *L, const detail::string_view& value)
    {
        return lua_pushstring(L, value.data()), 1;
    }
};

template <>
struct LuaValueWriter<UserObject>
{
    static inline int push(lua_State *L, const UserObject& value)
    {
        return pushUserObject(L, value);
    }
};

//-----------------------------------------------------------------------------
// Handle returning copies

template <typename R, typename U = void> struct CallReturnCopy;

template <typename R>
struct CallReturnCopy<R, typename std::enable_if<!detail::IsUserType<R>::value>::type>
{
    static inline int value(lua_State *L, R&& o) {return LuaValueWriter<R>::push(L, o);}
};

template <typename R>
struct CallReturnCopy<R, typename std::enable_if<detail::IsUserType<R>::value>::type>
{
    static inline int value(lua_State *L, R&& o)
    {return LuaValueWriter<UserObject>::push(L, UserObject::makeCopy(std::forward<R>(o)));}
};

//-----------------------------------------------------------------------------
// Handle returning internal references

template <typename R, typename U = void> struct CallReturnInternalRef;

template <typename R>
struct CallReturnInternalRef<R,
    typename std::enable_if<
        !detail::IsUserType<R>::value
        && !std::is_same<typename detail::RawType<R>::Type, UserObject>::value
    >::type>
{
    static inline int value(lua_State *L, R&& o) {return LuaValueWriter<R>::push(L, o);}
};

template <typename R>
struct CallReturnInternalRef<R,
    typename std::enable_if<
        detail::IsUserType<R>::value
        || std::is_same<typename detail::RawType<R>::Type, UserObject>::value
    >::type>
{
    static inline int value(lua_State *L, R&& o)
    {
        return LuaValueWriter<UserObject>::push(L, UserObject::makeRef(std::forward<R>(o)));
    }
};

//-----------------------------------------------------------------------------
// Choose which returner to use, based on policy
//  - map policy kind to actionable policy type

template <typename Policies_t, typename R> struct ChooseCallReturner;

template <typename... Ps, typename R>
struct ChooseCallReturner<std::tuple<policy::ReturnCopy, Ps...>, R>
{
    typedef CallReturnCopy<R> type;
};

template <typename... Ps, typename R>
struct ChooseCallReturner<std::tuple<policy::ReturnInternalRef, Ps...>, R>
{
    typedef CallReturnInternalRef<R> type;
};

template <typename R>
struct ChooseCallReturner<std::tuple<>, R> // default
{
    typedef CallReturnCopy<R> type;
};

template <typename P, typename... Ps, typename R>
struct ChooseCallReturner<std::tuple<P, Ps...>, R> // recurse
{
    typedef typename ChooseCallReturner<std::tuple<Ps...>, R>::type type;
};

//-----------------------------------------------------------------------------
// Convert Lua call arguments to C++ types.

template <typename P, typename U = void> struct LuaValueReader {};

template <typename P>
struct LuaValueReader<P, typename std::enable_if<std::is_integral<P>::value>::type>
{
    typedef P ParamType;
    static ParamType convert(lua_State* L, std::size_t index)
    {
        return lua_tointeger(L, index);
    }
};

template <typename P>
struct LuaValueReader<P, typename std::enable_if<std::is_floating_point<P>::value>::type>
{
    typedef P ParamType;
    static ParamType convert(lua_State* L, std::size_t index)
    {
        return lua_tonumber(L, index);
    }
};

template <typename P>
struct LuaValueReader<P, typename std::enable_if<std::is_enum<P>::value>::type>
{
    typedef P ParamType;
    static ParamType convert(lua_State* L, std::size_t index)
    {
        const lua_Integer i = lua_tointeger(L, index);
        return static_cast<P>(i);
    }
};

template <>
struct LuaValueReader<detail::string_view>
{
    typedef detail::string_view ParamType;
    static ParamType convert(lua_State* L, std::size_t index)
    {
        return ParamType(lua_tostring(L, index));
    }
};

template <typename P>
struct LuaValueReader<P, typename std::enable_if<detail::IsUserType<P>::value>::type>
{
    typedef P ParamType;
    static ParamType convert(lua_State* L, std::size_t index)
    {
        if (!lua_isuserdata(L, index))
        {
            luaL_error(L, "Argument %d: expecting user data", index);
        }
        
        UserObject *uobj = reinterpret_cast<UserObject*>(lua_touserdata(L, 1));
        
        return uobj->ref<typename std::remove_reference<ParamType>::type>();
    }
};

//-----------------------------------------------------------------------------
// Object function call helper to allow specialisation by return type. Applies policies.

template <typename P>
struct ConvertArgs
{
//    typedef typename ::ponder::detail::RawType<P>::Type Raw;
//    static constexpr ValueKind kind = ponder_ext::ValueMapper<Raw>::kind;
    typedef LuaValueReader<P> Convertor;
    
    static typename Convertor::ParamType convert(lua_State* L, std::size_t index)
    {
        return Convertor::convert(L, index+1);
    }
};

template <typename R, typename FTraits, typename FPolicies>
class CallHelper
{
public:
    
    template<typename F, typename... A, size_t... Is>
    static int call(F func, lua_State* L, _PONDER_SEQNS::index_sequence<Is...>)
    {
        typedef typename ChooseCallReturner<FPolicies, R>::type CallReturner;
        return CallReturner::value(L, func(ConvertArgs<A>::convert(L, Is)...));
    }
};

// Specialization of CallHelper for functions returning void
template <typename FTraits, typename FPolicies>
class CallHelper<void, FTraits, FPolicies>
{
public:
    
    template<typename F, typename... A, size_t... Is>
    static int call(F func, lua_State* L, _PONDER_SEQNS::index_sequence<Is...>)
    {
        func(ConvertArgs<A>::convert(L, Is)...);
        return 0; // return nil
    }
};

//-----------------------------------------------------------------------------
// Convert traits to callable function wrapper. Generic for all function types.

template <typename R, typename P> struct FunctionWrapper;

template <typename R, typename... P> struct FunctionWrapper<R, std::tuple<P...>>
{
    typedef typename std::function<R(P...)> Type;
    
    template <typename F, typename FTraits, typename FPolicies>
    static int call(F func, lua_State* L)
    {
        typedef _PONDER_SEQNS::make_index_sequence<sizeof...(P)> ArgEnumerator;
        
        return CallHelper<R, FTraits, FPolicies>::template call<F, P...>(func, L, ArgEnumerator());
    }
};

//-----------------------------------------------------------------------------
// Base for runtime function caller

class FunctionCaller
{
public:
    FunctionCaller(const IdRef name, int (*fn)(lua_State*) = nullptr)
        :   m_name(name)
        ,   m_luaFunc(fn)
    {}
    
    FunctionCaller(const FunctionCaller&) = delete; // no copying
    virtual ~FunctionCaller() {}
    
    const IdRef name() const { return m_name; }

    void pushFunction(lua_State* L)
    {
        lua_pushlightuserdata(L, (void*) this);
        lua_pushcclosure(L, m_luaFunc, 1);
    }
    
private:
    const IdRef m_name;
    int (*m_luaFunc)(lua_State*);
};

// The FunctionImpl class is a template which is specialized according to the
// underlying function prototype.
template <typename F, typename FTraits, typename FPolicies>
class FunctionCallerImpl : public FunctionCaller
{
public:
    
    FunctionCallerImpl(IdRef name, F function)
    :   FunctionCaller(name, &call)
    ,   m_function(function)
    {}
    
private:
    
    typedef FunctionCallerImpl<F, FTraits, FPolicies> ThisType;
    
    typedef typename FTraits::Details::FunctionCallTypes CallTypes;
    typedef FunctionWrapper<typename FTraits::ReturnType, CallTypes> FunctionType;
    
    typename FunctionType::Type m_function; // Object containing the actual function to call
    
    static int call(lua_State *L)
    {
        lua_pushvalue(L, lua_upvalueindex(1));
        ThisType *self = reinterpret_cast<ThisType*>(lua_touserdata(L, -1));
        lua_pop(L, 1);

        return FunctionType::template
            call<decltype(m_function), FTraits, FPolicies>(self->m_function, L);
    }
};
    
} // namespace impl
} // namespace lua
} // namespace ponder

#endif // PONDER_USES_DETAIL_LUA_HPP
