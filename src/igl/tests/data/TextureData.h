/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace igl {
namespace tests {
namespace data {
namespace texture {
// clang-format off

const unsigned int TEX_RGBA_2x2[] = {0x11223344, 0x11111111,
                                     0x22222222, 0x33333333};

const unsigned int TEX_RGBA_GRAY_4x4[] = {0x888888FF, 0x888888FF, 0x888888FF, 0x888888FF,
                                          0x888888FF, 0x888888FF, 0x888888FF, 0x888888FF,
                                          0x888888FF, 0x888888FF, 0x888888FF, 0x888888FF,
                                          0x888888FF, 0x888888FF, 0x888888FF, 0x888888FF};

const unsigned int TEX_RGBA_RED_ALPHA_128_4x4[] = {0x80000080, 0x80000080, 0x80000080, 0x80000080,
                                          0x80000080, 0x80000080, 0x80000080, 0x80000080,
                                          0x80000080, 0x80000080, 0x80000080, 0x80000080,
                                          0x80000080, 0x80000080, 0x80000080, 0x80000080};

const unsigned int TEX_RGBA_BLUE_ALPHA_127_4x4[] = {0x00007F7F, 0x00007F7F, 0x00007F7F, 0x00007F7F,
                                          0x00007F7F, 0x00007F7F, 0x00007F7F, 0x00007F7F,
                                          0x00007F7F, 0x00007F7F, 0x00007F7F, 0x00007F7F,
                                          0x00007F7F, 0x00007F7F, 0x00007F7F, 0x00007F7F};

const unsigned int TEX_RGBA_MISC1_4x4[] = {0x00000000, 0x11111111, 0x22222222, 0x33333333,
                                           0x44444444, 0x55555555, 0x66666666, 0x77777777,
                                           0x88888888, 0x99999999, 0xAAAAAAAA, 0xBBBBBBBB,
                                           0xCCCCCCCC, 0xDDDDDDDD, 0xEEEEEEEE, 0xFFFFFFFF};

// clang-format on
} // namespace texture
} // namespace data
} // namespace tests
} // namespace igl
