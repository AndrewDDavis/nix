#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "config.hh"
#include "eval.hh"
#include "globals.hh"
#include "util.hh"

#include "nix_api_expr.h"
#include "nix_api_expr_internal.h"
#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"

#ifdef HAVE_BOEHMGC
#include <mutex>
#define GC_INCLUDE_NEW 1
#include "gc_cpp.h"
#endif

nix_err nix_libexpr_init(nix_c_context *context) {
  if (context)
    context->last_err_code = NIX_OK;
  {
    auto ret = nix_libutil_init(context);
    if (ret != NIX_OK)
      return ret;
  }
  {
    auto ret = nix_libstore_init(context);
    if (ret != NIX_OK)
      return ret;
  }
  try {
    nix::initGC();
  }
  NIXC_CATCH_ERRS
}

nix_err nix_expr_eval_from_string(nix_c_context *context, State *state,
                                  const char *expr, const char *path,
                                  Value *value) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    nix::Expr *parsedExpr = state->state.parseExprFromString(
        expr, state->state.rootPath(nix::CanonPath(path)));
    state->state.eval(parsedExpr, *(nix::Value *)value);
  }
  NIXC_CATCH_ERRS
}

nix_err nix_value_call(nix_c_context *context, State *state, Value *fn,
                       Value *arg, Value *value) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    state->state.callFunction(*(nix::Value *)fn, *(nix::Value *)arg,
                              *(nix::Value *)value, nix::noPos);
  }
  NIXC_CATCH_ERRS
}

nix_err nix_value_force(nix_c_context *context, State *state, Value *value) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    state->state.forceValue(*(nix::Value *)value, nix::noPos);
  }
  NIXC_CATCH_ERRS
}

nix_err nix_value_force_deep(nix_c_context *context, State *state,
                             Value *value) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    state->state.forceValueDeep(*(nix::Value *)value);
  }
  NIXC_CATCH_ERRS
}

State *nix_state_create(nix_c_context *context, const char **searchPath_c,
                        Store *store) {
  if (context)
    context->last_err_code = NIX_OK;
  try {
    nix::Strings searchPath;
    if (searchPath_c != nullptr)
      for (size_t i = 0; searchPath_c[i] != nullptr; i++)
        searchPath.push_back(searchPath_c[i]);

    return new State{
        nix::EvalState(nix::SearchPath::parse(searchPath), store->ptr)};
  }
  NIXC_CATCH_ERRS_NULL
}

void nix_state_free(State *state) { delete state; }

#ifdef HAVE_BOEHMGC
std::unordered_map<
    const void *, unsigned int, std::hash<const void *>,
    std::equal_to<const void *>,
    traceable_allocator<std::pair<const void *const, unsigned int>>>
    nix_refcounts;

std::mutex nix_refcount_lock;

void nix_gc_incref(const void *p) {
  std::scoped_lock lock(nix_refcount_lock);
  auto f = nix_refcounts.find(p);
  if (f != nix_refcounts.end()) {
    f->second++;
  } else {
    nix_refcounts[p] = 1;
  }
}

void nix_gc_decref(const void *p) {
  std::scoped_lock lock(nix_refcount_lock);
  auto f = nix_refcounts.find(p);
  if (f != nix_refcounts.end()) {
    if (f->second == 1)
      nix_refcounts.erase(f);
    else
      f->second--;
  }
  // todo: else { throw? }
}

#else
void nix_gc_incref(const void *){};
void nix_gc_decref(const void *){};
#endif

void nix_gc_register_finalizer(void *obj, void *cd,
                               void (*finalizer)(void *obj, void *cd)) {
#ifdef HAVE_BOEHMGC
  GC_REGISTER_FINALIZER(obj, finalizer, cd, 0, 0);
#endif
}
