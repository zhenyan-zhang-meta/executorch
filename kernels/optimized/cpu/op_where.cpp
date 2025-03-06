/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include <executorch/kernels/portable/cpu/util/elementwise_util.h>
#include <executorch/runtime/kernel/kernel_includes.h>
#include <iostream>

namespace torch {
namespace executor {
namespace native {

Tensor& opt_where_out(
    KernelRuntimeContext& ctx,
    const Tensor& cond,
    const Tensor& a,
    const Tensor& b,
    Tensor& out) {
  // Common Dtype
  ScalarType common_type = promoteTypes(a.scalar_type(), b.scalar_type());

  // Check Common Dtype
  ET_KERNEL_CHECK(ctx, common_type == out.scalar_type(), InvalidArgument, out);

  // Check Dim Order
  ET_KERNEL_CHECK(
      ctx, tensors_have_same_dim_order(cond, a, b, out), InvalidArgument, out);

  // Resize
  ET_KERNEL_CHECK(
      ctx,
      resize_to_broadcast_target_size(a, b, cond, out) == Error::Ok,
      InvalidArgument,
      out);

  // Compute Dtype
  ScalarType compute_type = utils::get_compute_type(common_type);

  // @lint-ignore CLANGTIDY facebook-hte-CArray
  static constexpr const char op_name[] = "where.self_out";

  if (a.scalar_type() == b.scalar_type() &&
      a.scalar_type() == out.scalar_type() && a.scalar_type() == compute_type &&
      // Using a Byte tensor for cond has been deprecated for a long time.
      cond.scalar_type() == ScalarType::Bool) {
    auto out_numel = out.numel();
    ET_SWITCH_REALB_TYPES(compute_type, ctx, op_name, CTYPE_COMPUTE, [&]() {
      const bool a_is_broadcasted = !out.sizes().equals(a.sizes());
      const bool b_is_broadcasted = !out.sizes().equals(b.sizes());
      const bool cond_is_broadcasted = !out.sizes().equals(cond.sizes());
      const bool any_is_broadcasted =
          (a_is_broadcasted || b_is_broadcasted || cond_is_broadcasted);
      const CTYPE_COMPUTE* const data_a = a.const_data_ptr<CTYPE_COMPUTE>();
      const CTYPE_COMPUTE* const data_b = b.const_data_ptr<CTYPE_COMPUTE>();
      const bool* const data_cond = cond.const_data_ptr<bool>();
      CTYPE_COMPUTE* const data_out = out.data_ptr<CTYPE_COMPUTE>();
      if (any_is_broadcasted) {
        for (const auto [out_index, a_index, b_index, cond_index] :
             BroadcastIndexesRange<3>(out, a, b, cond)) {
          data_out[out_index] =
              data_cond[cond_index] ? data_a[a_index] : data_b[b_index];
        }
      } else {
        for (const auto i : c10::irange(out_numel)) {
          data_out[i] = data_cond[i] ? data_a[i] : data_b[i];
        }
      }
    });
  } else {
    // Fall back for mixed dtype to keep code size and compile time
    // reasonable.
    ET_SWITCH_REALB_TYPES(compute_type, ctx, op_name, CTYPE_COMPUTE, [&]() {
      utils::apply_tritensor_elementwise_fn<CTYPE_COMPUTE, op_name>(
          [](const CTYPE_COMPUTE val_a,
             const CTYPE_COMPUTE val_b,
             const CTYPE_COMPUTE val_c) { return val_c ? val_a : val_b; },
          ctx,
          a,
          utils::SupportedTensorDtypes::REALHBBF16,
          b,
          utils::SupportedTensorDtypes::REALHBBF16,
          cond,
          utils::SupportedTensorDtypes::BOOL_OR_BYTE,
          out,
          utils::SupportedTensorDtypes::SAME_AS_COMMON);
    });
  }

  return out;
}

} // namespace native
} // namespace executor
} // namespace torch
