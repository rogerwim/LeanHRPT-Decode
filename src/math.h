/*
 * LeanHRPT Decode
 * Copyright (C) 2021 Xerbo
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef LEANHRPT_MATH_H
#define LEANHRPT_MATH_H

#define RAD2DEG (180.0/M_PI)
#define DEG2RAD (M_PI/180.0)

inline double deg2rad(double deg) {
    return deg * DEG2RAD;
}
inline double rad2deg(double rad) {
    return rad * RAD2DEG;
}

template<typename T>
inline T clamp(T v, T lo, T hi) {
    return std::max(lo, std::min(hi, v));
}

template<typename T>
inline T lerp(T a, T b, T x) {
    return a*(1.0-x) + b*x;
}

#endif
