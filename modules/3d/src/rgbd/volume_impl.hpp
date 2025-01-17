// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html

#ifndef OPENCV_3D_VOLUME_IMPL_HPP
#define OPENCV_3D_VOLUME_IMPL_HPP

#include <iostream>

#include "../precomp.hpp"
#include "hash_tsdf_functions.hpp"

namespace cv
{

class Volume::Impl
{
private:
    // TODO: make debug function, which show histogram of volume points values
    // make this function run with debug lvl == 10
public:
    Impl(const VolumeSettings& settings);
    virtual ~Impl() {};

    virtual void integrate(const OdometryFrame& frame, InputArray pose) = 0;
    virtual void integrate(InputArray depth, InputArray pose) = 0;
    virtual void integrate(InputArray depth, InputArray image, InputArray pose) = 0;

    virtual void raycast(InputArray cameraPose, OdometryFrame& outFrame) const = 0;
    virtual void raycast(InputArray cameraPose, OutputArray points, OutputArray normals, OutputArray colors) const = 0;

    virtual void raycast(InputArray cameraPose, int height, int width, OdometryFrame& outFrame) const = 0;
    virtual void raycast(InputArray cameraPose, int height, int width, OutputArray points, OutputArray normals, OutputArray colors) const = 0;

    virtual void fetchNormals(InputArray points, OutputArray normals) const = 0;
    virtual void fetchPointsNormals(OutputArray points, OutputArray normals) const = 0;
    virtual void fetchPointsNormalsColors(OutputArray points, OutputArray normals, OutputArray colors) const = 0;

    virtual void reset() = 0;
    virtual int getVisibleBlocks() const = 0;
    virtual size_t getTotalVolumeUnits() const = 0;

public:
    const VolumeSettings& settings;
#ifdef HAVE_OPENCL
    const bool useGPU;
#endif
};


class TsdfVolume : public Volume::Impl
{
public:
    TsdfVolume(const VolumeSettings& settings);
    ~TsdfVolume();

    virtual void integrate(const OdometryFrame& frame, InputArray pose) override;
    virtual void integrate(InputArray depth, InputArray pose) override;
    virtual void integrate(InputArray depth, InputArray image, InputArray pose) override;
    virtual void raycast(InputArray cameraPose, OdometryFrame& outFrame) const override;
    virtual void raycast(InputArray cameraPose, OutputArray points, OutputArray normals, OutputArray colors) const override;
    virtual void raycast(InputArray cameraPose, int height, int width, OdometryFrame& outFrame) const override;
    virtual void raycast(InputArray cameraPose, int height, int width, OutputArray points, OutputArray normals, OutputArray colors) const override;

    virtual void fetchNormals(InputArray points, OutputArray normals) const override;
    virtual void fetchPointsNormals(OutputArray points, OutputArray normals) const override;
    virtual void fetchPointsNormalsColors(OutputArray points, OutputArray normals, OutputArray colors) const override;

    virtual void reset() override;
    virtual int getVisibleBlocks() const override;
    virtual size_t getTotalVolumeUnits() const override;

public:
    Vec6f frameParams;
#ifndef HAVE_OPENCL
    Mat pixNorms;
    // See zFirstMemOrder arg of parent class constructor
    // for the array layout info
    // Consist of Voxel elements
    Mat volume;
#else
    //temporary solution
    Mat cpu_pixNorms;
    Mat cpu_volume;
    UMat gpu_pixNorms;
    UMat gpu_volume;
#endif
};


typedef std::unordered_set<cv::Vec3i, tsdf_hash> VolumeUnitIndexSet;
typedef std::unordered_map<cv::Vec3i, VolumeUnit, tsdf_hash> VolumeUnitIndexes;

class HashTsdfVolume : public Volume::Impl
{
public:
    HashTsdfVolume(const VolumeSettings& settings);
    ~HashTsdfVolume();

    virtual void integrate(const OdometryFrame& frame, InputArray pose) override;
    virtual void integrate(InputArray depth, InputArray pose) override;
    virtual void integrate(InputArray depth, InputArray image, InputArray pose) override;
    virtual void raycast(InputArray cameraPose, OdometryFrame& outFrame) const override;
    virtual void raycast(InputArray cameraPose, OutputArray points, OutputArray normals, OutputArray colors) const override;
    virtual void raycast(InputArray cameraPose, int height, int width, OdometryFrame& outFrame) const override;
    virtual void raycast(InputArray cameraPose, int height, int width, OutputArray points, OutputArray normals, OutputArray colors) const override;

    virtual void fetchNormals(InputArray points, OutputArray normals) const override;
    virtual void fetchPointsNormals(OutputArray points, OutputArray normals) const override;
    virtual void fetchPointsNormalsColors(OutputArray points, OutputArray normals, OutputArray colors) const override;

    virtual void reset() override;
    virtual int getVisibleBlocks() const override;
    virtual size_t getTotalVolumeUnits() const override;
public:
    int lastVolIndex;
    int lastFrameId;
    Vec6f frameParams;
    int volumeUnitDegree;

#ifndef HAVE_OPENCL
    Mat volUnitsData;
    Mat pixNorms;
    VolumeUnitIndexes volumeUnits;
#else
    VolumeUnitIndexes cpu_volumeUnits;

    Mat cpu_volUnitsData;
    Mat cpu_pixNorms;
    UMat gpu_volUnitsData;
    UMat gpu_pixNorms;

    int bufferSizeDegree;
    // per-volume-unit data
    UMat lastVisibleIndices;
    UMat isActiveFlags;
    //TODO: remove it when there's no CPU parts
    Mat volUnitsDataCopy;
    //TODO: move indexes.volumes to GPU
    CustomHashSet hashTable;
#endif
};


class ColorTsdfVolume : public Volume::Impl
{
public:
    ColorTsdfVolume(const VolumeSettings& settings);
    ~ColorTsdfVolume();

    virtual void integrate(const OdometryFrame& frame, InputArray pose) override;
    virtual void integrate(InputArray depth, InputArray pose) override;
    virtual void integrate(InputArray depth, InputArray image, InputArray pose) override;
    virtual void raycast(InputArray cameraPose, OdometryFrame& outFrame) const override;
    virtual void raycast(InputArray cameraPose, OutputArray points, OutputArray normals, OutputArray colors) const override;
    virtual void raycast(InputArray cameraPose, int height, int width, OdometryFrame& outFrame) const override;
    virtual void raycast(InputArray cameraPose, int height, int width, OutputArray points, OutputArray normals, OutputArray colors) const override;

    virtual void fetchNormals(InputArray points, OutputArray normals) const override;
    virtual void fetchPointsNormals(OutputArray points, OutputArray normals) const override;
    virtual void fetchPointsNormalsColors(OutputArray points, OutputArray normals, OutputArray colors) const override;

    virtual void reset() override;
    virtual int getVisibleBlocks() const override;
    virtual size_t getTotalVolumeUnits() const override;
private:
    Vec4i volStrides;
    Vec6f frameParams;
    Mat pixNorms;
    // See zFirstMemOrder arg of parent class constructor
    // for the array layout info
    // Consist of Voxel elements
    Mat volume;
};


Volume::Volume()
{
    VolumeSettings settings;
    this->impl = makePtr<TsdfVolume>(settings);
}
Volume::Volume(VolumeType vtype, const VolumeSettings& settings)
{
    switch (vtype)
    {
    case VolumeType::TSDF:
        this->impl = makePtr<TsdfVolume>(settings);
        break;
    case VolumeType::HashTSDF:
        this->impl = makePtr<HashTsdfVolume>(settings);
        break;
    case VolumeType::ColorTSDF:
        this->impl = makePtr<ColorTsdfVolume>(settings);
        break;
    default:
        CV_Error(Error::StsInternal, "Incorrect OdometryType, you are able to use only { ICP, RGB, RGBD }");
        break;
    }
}
Volume::~Volume() {}

void Volume::integrate(const OdometryFrame& frame, InputArray pose) { this->impl->integrate(frame, pose); }
void Volume::integrate(InputArray depth, InputArray pose) { this->impl->integrate(depth, pose); }
void Volume::integrate(InputArray depth, InputArray image, InputArray pose) { this->impl->integrate(depth, image, pose); }
void Volume::raycast(InputArray cameraPose, OdometryFrame& outFrame) const { this->impl->raycast(cameraPose, outFrame); }
void Volume::raycast(InputArray cameraPose, OutputArray _points, OutputArray _normals, OutputArray _colors) const { this->impl->raycast(cameraPose, _points, _normals, _colors); }
void Volume::raycast(InputArray cameraPose, int height, int width, OdometryFrame& outFrame) const { this->impl->raycast(cameraPose, height, width, outFrame); }
void Volume::raycast(InputArray cameraPose, int height, int width, OutputArray _points, OutputArray _normals, OutputArray _colors) const { this->impl->raycast(cameraPose, height, width, _points, _normals, _colors); }
void Volume::fetchNormals(InputArray points, OutputArray normals) const { this->impl->fetchNormals(points, normals); }
void Volume::fetchPointsNormals(OutputArray points, OutputArray normals) const { this->impl->fetchPointsNormals(points, normals); }
void Volume::fetchPointsNormalsColors(OutputArray points, OutputArray normals, OutputArray colors) const { this->impl->fetchPointsNormalsColors(points, normals, colors); };

void Volume::reset() { this->impl->reset(); }
int Volume::getVisibleBlocks() const { return this->impl->getVisibleBlocks(); }
size_t Volume::getTotalVolumeUnits() const { return this->impl->getTotalVolumeUnits(); }


}

#endif // !OPENCV_3D_VOLUME_IMPL_HPP
