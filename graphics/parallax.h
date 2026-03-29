/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef GRAPHICS_PARALLAX_H
#define GRAPHICS_PARALLAX_H

#include <cmath>

#include "math/vector2d.h"

namespace Graphics {
namespace Parallax {

struct ScreenShiftConfig {
	float maxOffsetXPixels;
	float maxOffsetYPixels;
	float depthExponent;
	float minDepthWeight;
};

struct PerspectiveShiftConfig {
	float horizontalFovDegrees;
	float nearClip;
	float farClip;
	float screenPlaneDistance;
	float aspectRatio;
	float viewportWidthPixels;
	float viewportHeightPixels;
};

inline float clamp01(float value) {
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

inline float decodeNormalizedDepth(uint16 encodedDepth) {
	return clamp01(1.0f - ((float)encodedDepth / 65535.0f));
}

inline float computeDepthWeight(float normalizedDepth, float depthExponent, float minDepthWeight) {
	const float shapedDepth = powf(clamp01(normalizedDepth), depthExponent);
	return minDepthWeight + (1.0f - minDepthWeight) * shapedDepth;
}

inline Math::Vector2d computePixelOffset(float normalizedInputX, float normalizedInputY, float factor, const ScreenShiftConfig &config) {
	return Math::Vector2d(
		-normalizedInputX * config.maxOffsetXPixels * factor,
		-normalizedInputY * config.maxOffsetYPixels * factor
	);
}

inline Math::Vector2d computeUVShift(float normalizedInputX, float normalizedInputY, float factor, int width, int height, float depthWeight, const ScreenShiftConfig &config) {
	if (width <= 0 || height <= 0)
		return Math::Vector2d();

	const Math::Vector2d pixelOffset = computePixelOffset(normalizedInputX, normalizedInputY, factor, config) * depthWeight;
	return Math::Vector2d(pixelOffset.getX() / (float)width, pixelOffset.getY() / (float)height);
}

inline float linearizePerspectiveDepth(float windowDepth, float nearClip, float farClip) {
	const float z = clamp01(windowDepth) * 2.0f - 1.0f;
	const float denom = farClip + nearClip - z * (farClip - nearClip);
	if (fabsf(denom) < 0.0001f)
		return nearClip;
	return (2.0f * nearClip * farClip) / denom;
}

inline Math::Vector2d computePerspectivePixelShift(const Math::Vector2d &cameraPlaneShift, float linearDepth, const PerspectiveShiftConfig &config) {
	if (linearDepth <= 0.0001f || config.screenPlaneDistance <= 0.0001f ||
		config.viewportWidthPixels <= 0.0f || config.viewportHeightPixels <= 0.0f)
		return Math::Vector2d();

	const float tanHalfFovX = tanf((config.horizontalFovDegrees * ((float)M_PI / 180.0f)) * 0.5f);
	const float tanHalfFovY = tanHalfFovX * config.aspectRatio;
	if (fabsf(tanHalfFovX) < 0.0001f || fabsf(tanHalfFovY) < 0.0001f)
		return Math::Vector2d();

	return Math::Vector2d(
		(cameraPlaneShift.getX() * config.viewportWidthPixels * 0.5f) *
			((1.0f / config.screenPlaneDistance) - (1.0f / linearDepth)) / tanHalfFovX,
		(cameraPlaneShift.getY() * config.viewportHeightPixels * 0.5f) *
			((1.0f / config.screenPlaneDistance) - (1.0f / linearDepth)) / tanHalfFovY
	);
}

} // namespace Parallax
} // namespace Graphics

#endif
