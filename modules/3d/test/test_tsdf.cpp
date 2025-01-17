// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html

#include "test_precomp.hpp"

namespace opencv_test {
namespace {

using namespace cv;

/** Reprojects screen point to camera space given z coord. */
struct Reprojector
{
    Reprojector() {}
    inline Reprojector(Matx33f intr)
    {
        fxinv = 1.f / intr(0, 0), fyinv = 1.f / intr(1, 1);
        cx = intr(0, 2), cy = intr(1, 2);
    }
    template<typename T>
    inline cv::Point3_<T> operator()(cv::Point3_<T> p) const
    {
        T x = p.z * (p.x - cx) * fxinv;
        T y = p.z * (p.y - cy) * fyinv;
        return cv::Point3_<T>(x, y, p.z);
    }

    float fxinv, fyinv, cx, cy;
};

template<class Scene>
struct RenderInvoker : ParallelLoopBody
{
    RenderInvoker(Mat_<float>& _frame, Affine3f _pose,
        Reprojector _reproj, float _depthFactor, bool _onlySemisphere)
        : ParallelLoopBody(),
        frame(_frame),
        pose(_pose),
        reproj(_reproj),
        depthFactor(_depthFactor),
        onlySemisphere(_onlySemisphere)
    { }

    virtual void operator ()(const cv::Range& r) const
    {
        for (int y = r.start; y < r.end; y++)
        {
            float* frameRow = frame[y];
            for (int x = 0; x < frame.cols; x++)
            {
                float pix = 0;

                Point3f orig = pose.translation();
                // direction through pixel
                Point3f screenVec = reproj(Point3f((float)x, (float)y, 1.f));
                float xyt = 1.f / (screenVec.x * screenVec.x +
                    screenVec.y * screenVec.y + 1.f);
                Point3f dir = normalize(Vec3f(pose.rotation() * screenVec));
                // screen space axis
                dir.y = -dir.y;

                const float maxDepth = 20.f;
                const float maxSteps = 256;
                float t = 0.f;
                for (int step = 0; step < maxSteps && t < maxDepth; step++)
                {
                    Point3f p = orig + dir * t;
                    float d = Scene::map(p, onlySemisphere);
                    if (d < 0.000001f)
                    {
                        float depth = std::sqrt(t * t * xyt);
                        pix = depth * depthFactor;
                        break;
                    }
                    t += d;
                }

                frameRow[x] = pix;
            }
        }
    }

    Mat_<float>& frame;
    Affine3f pose;
    Reprojector reproj;
    float depthFactor;
    bool onlySemisphere;
};

template<class Scene>
struct RenderColorInvoker : ParallelLoopBody
{
    RenderColorInvoker(Mat_<Vec3f>& _frame, Affine3f _pose,
        Reprojector _reproj,
        float _depthFactor, bool _onlySemisphere) : ParallelLoopBody(),
        frame(_frame),
        pose(_pose),
        reproj(_reproj),
        depthFactor(_depthFactor),
        onlySemisphere(_onlySemisphere)
    { }

    virtual void operator ()(const cv::Range& r) const
    {
        for (int y = r.start; y < r.end; y++)
        {
            Vec3f* frameRow = frame[y];
            for (int x = 0; x < frame.cols; x++)
            {
                Vec3f pix = 0;

                Point3f orig = pose.translation();
                // direction through pixel
                Point3f screenVec = reproj(Point3f((float)x, (float)y, 1.f));
                Point3f dir = normalize(Vec3f(pose.rotation() * screenVec));
                // screen space axis
                dir.y = -dir.y;

                const float maxDepth = 20.f;
                const float maxSteps = 256;
                float t = 0.f;
                for (int step = 0; step < maxSteps && t < maxDepth; step++)
                {
                    Point3f p = orig + dir * t;
                    float d = Scene::map(p, onlySemisphere);
                    if (d < 0.000001f)
                    {
                        float m = 0.25f;
                        float p0 = float(abs(fmod(p.x, m)) > m / 2.f);
                        float p1 = float(abs(fmod(p.y, m)) > m / 2.f);
                        float p2 = float(abs(fmod(p.z, m)) > m / 2.f);

                        pix[0] = p0 + p1;
                        pix[1] = p1 + p2;
                        pix[2] = p0 + p2;

                        pix *= 128.f;
                        break;
                    }
                    t += d;
                }

                frameRow[x] = pix;
            }
        }
    }

    Mat_<Vec3f>& frame;
    Affine3f pose;
    Reprojector reproj;
    float depthFactor;
    bool onlySemisphere;
};


struct Scene
{
    virtual ~Scene() {}
    static Ptr<Scene> create(Size sz, Matx33f _intr, float _depthFactor, bool onlySemisphere);
    virtual Mat depth(Affine3f pose) = 0;
    virtual Mat rgb(Affine3f pose) = 0;
    virtual std::vector<Affine3f> getPoses() = 0;
};

struct SemisphereScene : Scene
{
    const int framesPerCycle = 72;
    const float nCycles = 0.25f;
    const Affine3f startPose = Affine3f(Vec3f(0.f, 0.f, 0.f), Vec3f(1.5f, 0.3f, -2.1f));

    Size frameSize;
    Matx33f intr;
    float depthFactor;
    bool onlySemisphere;

    SemisphereScene(Size sz, Matx33f _intr, float _depthFactor, bool _onlySemisphere) :
        frameSize(sz), intr(_intr), depthFactor(_depthFactor), onlySemisphere(_onlySemisphere)
    { }

    static float map(Point3f p, bool onlySemisphere)
    {
        float plane = p.y + 0.5f;
        Point3f spherePose = p - Point3f(-0.0f, 0.3f, 1.1f);
        float sphereRadius = 0.5f;
        float sphere = (float)cv::norm(spherePose) - sphereRadius;
        float sphereMinusBox = sphere;

        float subSphereRadius = 0.05f;
        Point3f subSpherePose = p - Point3f(0.3f, -0.1f, -0.3f);
        float subSphere = (float)cv::norm(subSpherePose) - subSphereRadius;

        float res;
        if (!onlySemisphere)
            res = min({ sphereMinusBox, subSphere, plane });
        else
            res = sphereMinusBox;

        return res;
    }

    Mat depth(Affine3f pose) override
    {
        Mat_<float> frame(frameSize);
        Reprojector reproj(intr);

        Range range(0, frame.rows);
        parallel_for_(range, RenderInvoker<SemisphereScene>(frame, pose, reproj, depthFactor, onlySemisphere));

        return std::move(frame);
    }

    Mat rgb(Affine3f pose) override
    {
        Mat_<Vec3f> frame(frameSize);
        Reprojector reproj(intr);

        Range range(0, frame.rows);
        parallel_for_(range, RenderColorInvoker<SemisphereScene>(frame, pose, reproj, depthFactor, onlySemisphere));

        return std::move(frame);
    }

    std::vector<Affine3f> getPoses() override
    {
        std::vector<Affine3f> poses;
        for (int i = 0; i < framesPerCycle * nCycles; i++)
        {
            float angle = (float)(CV_2PI * i / framesPerCycle);
            Affine3f pose;
            pose = pose.rotate(startPose.rotation());
            pose = pose.rotate(Vec3f(0.f, -0.5f, 0.f) * angle);
            pose = pose.translate(Vec3f(startPose.translation()[0] * sin(angle),
                startPose.translation()[1],
                startPose.translation()[2] * cos(angle)));
            poses.push_back(pose);
        }

        return poses;
    }

};

Ptr<Scene> Scene::create(Size sz, Matx33f _intr, float _depthFactor, bool _onlySemisphere)
{
    return makePtr<SemisphereScene>(sz, _intr, _depthFactor, _onlySemisphere);
}

// this is a temporary solution
// ----------------------------

typedef cv::Vec4f ptype;
typedef cv::Mat_< ptype > Points;
typedef cv::Mat_< ptype > Colors;
typedef Points Normals;
typedef Size2i Size;

template<int p>
inline float specPow(float x)
{
    if (p % 2 == 0)
    {
        float v = specPow<p / 2>(x);
        return v * v;
    }
    else
    {
        float v = specPow<(p - 1) / 2>(x);
        return v * v * x;
    }
}

template<>
inline float specPow<0>(float /*x*/)
{
    return 1.f;
}

template<>
inline float specPow<1>(float x)
{
    return x;
}

inline cv::Vec3f fromPtype(const ptype& x)
{
    return cv::Vec3f(x[0], x[1], x[2]);
}

inline Point3f normalize(const Vec3f& v)
{
    double nv = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return v * (nv ? 1. / nv : 0.);
}

void renderPointsNormals(InputArray _points, InputArray _normals, OutputArray image, Affine3f lightPose)
{
    Size sz = _points.size();
    image.create(sz, CV_8UC4);

    Points  points = _points.getMat();
    Normals normals = _normals.getMat();

    Mat_<Vec4b> img = image.getMat();

    Range range(0, sz.height);
    const int nstripes = -1;
    parallel_for_(range, [&](const Range&)
        {
            for (int y = range.start; y < range.end; y++)
            {
                Vec4b* imgRow = img[y];
                const ptype* ptsRow = points[y];
                const ptype* nrmRow = normals[y];

                for (int x = 0; x < sz.width; x++)
                {
                    Point3f p = fromPtype(ptsRow[x]);
                    Point3f n = fromPtype(nrmRow[x]);

                    Vec4b color;

                    if (cvIsNaN(p.x) || cvIsNaN(p.y) || cvIsNaN(p.z))
                    {
                        color = Vec4b(0, 32, 0, 0);
                    }
                    else
                    {
                        const float Ka = 0.3f;  //ambient coeff
                        const float Kd = 0.5f;  //diffuse coeff
                        const float Ks = 0.2f;  //specular coeff
                        const int   sp = 20;  //specular power

                        const float Ax = 1.f;   //ambient color,  can be RGB
                        const float Dx = 1.f;   //diffuse color,  can be RGB
                        const float Sx = 1.f;   //specular color, can be RGB
                        const float Lx = 1.f;   //light color

                        Point3f l = normalize(lightPose.translation() - Vec3f(p));
                        Point3f v = normalize(-Vec3f(p));
                        Point3f r = normalize(Vec3f(2.f * n * n.dot(l) - l));

                        uchar ix = (uchar)((Ax * Ka * Dx + Lx * Kd * Dx * max(0.f, n.dot(l)) +
                            Lx * Ks * Sx * specPow<sp>(max(0.f, r.dot(v)))) * 255.f);
                        color = Vec4b(ix, ix, ix, 0);
                    }

                    imgRow[x] = color;
                }
            }
        }, nstripes);
}
void renderPointsNormalsColors(InputArray _points, InputArray, InputArray _colors, OutputArray image, Affine3f)
{
    Size sz = _points.size();
    image.create(sz, CV_8UC4);

    Points  points  = _points.getMat();
    Colors  colors  = _colors.getMat();

    Mat_<Vec4b> img = image.getMat();

    Range range(0, sz.height);
    const int nstripes = -1;
    parallel_for_(range, [&](const Range&)
        {
            for (int y = range.start; y < range.end; y++)
            {
                Vec4b* imgRow = img[y];
                const ptype* ptsRow = points[y];
                const ptype* clrRow = colors[y];

                for (int x = 0; x < sz.width; x++)
                {
                    Point3f p = fromPtype(ptsRow[x]);
                    Point3f c = fromPtype(clrRow[x]);

                    Vec4b color;

                    if (cvIsNaN(p.x) || cvIsNaN(p.y) || cvIsNaN(p.z)
                        || cvIsNaN(c.x) || cvIsNaN(c.y) || cvIsNaN(c.z))
                    {
                        color = Vec4b(0, 32, 0, 0);
                    }
                    else
                    {
                        color = Vec4b((uchar)c.x, (uchar)c.y, (uchar)c.z, (uchar)0);
                    }

                    imgRow[x] = color;
                }
            }
        }, nstripes);
}
// ----------------------------

void displayImage(Mat depth, Mat points, Mat normals, float depthFactor, Vec3f lightPose)
{
    Mat image;
    patchNaNs(points);
    imshow("depth", depth * (1.f / depthFactor / 4.f));
    renderPointsNormals(points, normals, image, lightPose);
    imshow("render", image);
    waitKey(2000);
    destroyAllWindows();
}

void displayColorImage(Mat depth, Mat rgb, Mat points, Mat normals, Mat colors, float depthFactor, Vec3f lightPose)
{
    Mat image;
    patchNaNs(points);
    imshow("depth", depth * (1.f / depthFactor / 4.f));
    imshow("rgb", rgb * (1.f / 255.f));
    renderPointsNormalsColors(points, normals, colors, image, lightPose);
    imshow("render", image);
    waitKey(2000);
    destroyAllWindows();
}

void normalsCheck(Mat normals)
{
    Vec4f vector;
    int counter = 0;
    for (auto pvector = normals.begin<Vec4f>(); pvector < normals.end<Vec4f>(); pvector++)
    {
        vector = *pvector;
        if (!cvIsNaN(vector[0]))
        {
            counter++;
            float length = vector[0] * vector[0] +
                vector[1] * vector[1] +
                vector[2] * vector[2];
            ASSERT_LT(abs(1 - length), 0.0001f) << "There is normal with length != 1";
        }
    }
    ASSERT_GT(counter, 0) << "There are not normals";
}

int counterOfValid(Mat points)
{
    Vec4f* v;
    int i, j;
    int count = 0;
    for (i = 0; i < points.rows; ++i)
    {
        v = (points.ptr<Vec4f>(i));
        for (j = 0; j < points.cols; ++j)
        {
            if ((v[j])[0] != 0 ||
                (v[j])[1] != 0 ||
                (v[j])[2] != 0)
            {
                count++;
            }
        }
    }
    return count;
}


enum class VolumeTestFunction
{
    RAYCAST = 0,
    FETCH_NORMALS = 1,
    FETCH_POINTS_NORMALS = 2
};

enum class VolumeTestSrcType
{
    MAT = 0,
    ODOMETRY_FRAME = 1
};

void normal_test_custom_framesize(VolumeType volumeType, VolumeTestFunction testFunction, VolumeTestSrcType testSrcType)
{
    VolumeSettings vs(volumeType);
    Volume volume(volumeType, vs);

    Size frameSize(vs.getRaycastWidth(), vs.getRaycastHeight());
    Matx33f intr;
    vs.getCameraIntegrateIntrinsics(intr);
    bool onlySemisphere = false;
    float depthFactor = vs.getDepthFactor();
    Vec3f lightPose = Vec3f::all(0.f);
    Ptr<Scene> scene = Scene::create(frameSize, intr, depthFactor, onlySemisphere);
    std::vector<Affine3f> poses = scene->getPoses();

    Mat depth = scene->depth(poses[0]);
    Mat rgb = scene->rgb(poses[0]);
    Mat points, normals, tmpnormals, colors;

    OdometryFrame odf;
    odf.setDepth(depth);
    odf.setImage(rgb);

    if (testSrcType == VolumeTestSrcType::MAT)
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.integrate(depth, rgb, poses[0].matrix);
        else
            volume.integrate(depth, poses[0].matrix);
    }
    else
    {
        volume.integrate(odf, poses[0].matrix);
    }

    if (testFunction == VolumeTestFunction::RAYCAST)
    {
        if (testSrcType == VolumeTestSrcType::MAT)
        {
            if (volumeType == VolumeType::ColorTSDF)
                volume.raycast(poses[0].matrix, frameSize.height, frameSize.width, points, normals, colors);
            else
                volume.raycast(poses[0].matrix, frameSize.height, frameSize.width, points, normals);
        }
        else if (testSrcType == VolumeTestSrcType::ODOMETRY_FRAME)
        {
            volume.raycast(poses[0].matrix, frameSize.height, frameSize.width, odf);
            odf.getPyramidAt(points, OdometryFramePyramidType::PYR_CLOUD, 0);
            odf.getPyramidAt(normals, OdometryFramePyramidType::PYR_NORM, 0);
            if (volumeType == VolumeType::ColorTSDF)
                odf.getPyramidAt(colors, OdometryFramePyramidType::PYR_IMAGE, 0);
        }
    }
    else if (testFunction == VolumeTestFunction::FETCH_NORMALS)
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.raycast(poses[0].matrix, frameSize.height, frameSize.width, points, tmpnormals, colors);
        else
            // hash_tsdf cpu don't works with raycast normals
            //volume.raycast(poses[0].matrix, frameSize.height, frameSize.width, points, tmpnormals);
            volume.fetchPointsNormals(points, tmpnormals);

        volume.fetchNormals(points, normals);
    }
    else if (testFunction == VolumeTestFunction::FETCH_POINTS_NORMALS)
    {
        volume.fetchPointsNormals(points, normals);
    }

    if (testFunction == VolumeTestFunction::RAYCAST && cvtest::debugLevel > 0)
    {
        if (volumeType == VolumeType::ColorTSDF)
            displayColorImage(depth, rgb, points, normals, colors, depthFactor, lightPose);
        else
            displayImage(depth, points, normals, depthFactor, lightPose);
    }

    normalsCheck(normals);
}

void normal_test_common_framesize(VolumeType volumeType, VolumeTestFunction testFunction, VolumeTestSrcType testSrcType)
{
    VolumeSettings vs(volumeType);
    Volume volume(volumeType, vs);

    Size frameSize(vs.getRaycastWidth(), vs.getRaycastHeight());
    Matx33f intr;
    vs.getCameraIntegrateIntrinsics(intr);
    bool onlySemisphere = false;
    float depthFactor = vs.getDepthFactor();
    Vec3f lightPose = Vec3f::all(0.f);
    Ptr<Scene> scene = Scene::create(frameSize, intr, depthFactor, onlySemisphere);
    std::vector<Affine3f> poses = scene->getPoses();

    Mat depth = scene->depth(poses[0]);
    Mat rgb = scene->rgb(poses[0]);
    Mat points, normals, tmpnormals, colors;

    OdometryFrame odf;
    odf.setDepth(depth);
    odf.setImage(rgb);

    if (testSrcType == VolumeTestSrcType::MAT)
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.integrate(depth, rgb, poses[0].matrix);
        else
            volume.integrate(depth, poses[0].matrix);
    }
    else
    {
        volume.integrate(odf, poses[0].matrix);
    }

    if (testFunction == VolumeTestFunction::RAYCAST)
    {
        if (testSrcType == VolumeTestSrcType::MAT)
        {
            if (volumeType == VolumeType::ColorTSDF)
                volume.raycast(poses[0].matrix, points, normals, colors);
            else
                volume.raycast(poses[0].matrix, points, normals);
        }
        else if (testSrcType == VolumeTestSrcType::ODOMETRY_FRAME)
        {
            volume.raycast(poses[0].matrix, odf);
            odf.getPyramidAt(points, OdometryFramePyramidType::PYR_CLOUD, 0);
            odf.getPyramidAt(normals, OdometryFramePyramidType::PYR_NORM, 0);
            if (volumeType == VolumeType::ColorTSDF)
                odf.getPyramidAt(colors, OdometryFramePyramidType::PYR_IMAGE, 0);
        }
    }
    else if (testFunction == VolumeTestFunction::FETCH_NORMALS)
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.raycast(poses[0].matrix, points, tmpnormals, colors);
        else
            // hash_tsdf cpu don't works with raycast normals
            //volume.raycast(poses[0].matrix, points, tmpnormals);
            volume.fetchPointsNormals(points, tmpnormals);

        volume.fetchNormals(points, normals);
    }
    else if (testFunction == VolumeTestFunction::FETCH_POINTS_NORMALS)
    {
        volume.fetchPointsNormals(points, normals);
    }

    if (testFunction == VolumeTestFunction::RAYCAST && cvtest::debugLevel > 0)
    {
        if (volumeType == VolumeType::ColorTSDF)
            displayColorImage(depth, rgb, points, normals, colors, depthFactor, lightPose);
        else
            displayImage(depth, points, normals, depthFactor, lightPose);
    }

    normalsCheck(normals);
}

void valid_points_test_custom_framesize(VolumeType volumeType, VolumeTestSrcType testSrcType)
{
    VolumeSettings vs(volumeType);
    Volume volume(volumeType, vs);

    Size frameSize(vs.getRaycastWidth(), vs.getRaycastHeight());
    Matx33f intr;
    vs.getCameraIntegrateIntrinsics(intr);
    bool onlySemisphere = true;
    float depthFactor = vs.getDepthFactor();
    Vec3f lightPose = Vec3f::all(0.f);
    Ptr<Scene> scene = Scene::create(frameSize, intr, depthFactor, onlySemisphere);
    std::vector<Affine3f> poses = scene->getPoses();

    Mat depth = scene->depth(poses[0]);
    Mat rgb = scene->rgb(poses[0]);
    Mat points, normals, colors, newPoints, newNormals;
    int anfas, profile;

    OdometryFrame odf;
    odf.setDepth(depth);
    odf.setImage(rgb);

    if (testSrcType == VolumeTestSrcType::MAT)
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.integrate(depth, rgb, poses[0].matrix);
        else
            volume.integrate(depth, poses[0].matrix);
    }
    else
    {
        volume.integrate(odf, poses[0].matrix);
    }

    if (testSrcType == VolumeTestSrcType::MAT) // Odometry frame or Mats
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.raycast(poses[0].matrix, frameSize.height, frameSize.width, points, normals, colors);
        else
            volume.raycast(poses[0].matrix, frameSize.height, frameSize.width, points, normals);
    }
    else if (testSrcType == VolumeTestSrcType::ODOMETRY_FRAME)
    {
        volume.raycast(poses[0].matrix, frameSize.height, frameSize.width, odf);
        odf.getPyramidAt(points, OdometryFramePyramidType::PYR_CLOUD, 0);
        odf.getPyramidAt(normals, OdometryFramePyramidType::PYR_NORM, 0);
        if (volumeType == VolumeType::ColorTSDF)
            odf.getPyramidAt(colors, OdometryFramePyramidType::PYR_IMAGE, 0);
    }

    patchNaNs(points);
    anfas = counterOfValid(points);

    if (cvtest::debugLevel > 0)
    {
        if (volumeType == VolumeType::ColorTSDF)
            displayColorImage(depth, rgb, points, normals, colors, depthFactor, lightPose);
        else
            displayImage(depth, points, normals, depthFactor, lightPose);
    }

    points.release();
    normals.release();

    if (testSrcType == VolumeTestSrcType::MAT) // Odometry frame or Mats
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.raycast(poses[17].matrix, frameSize.height, frameSize.width, points, normals, colors);
        else
            volume.raycast(poses[17].matrix, frameSize.height, frameSize.width, points, normals);
    }
    else if (testSrcType == VolumeTestSrcType::ODOMETRY_FRAME)
    {
        volume.raycast(poses[17].matrix, frameSize.height, frameSize.width, odf);
        odf.getPyramidAt(points, OdometryFramePyramidType::PYR_CLOUD, 0);
        odf.getPyramidAt(normals, OdometryFramePyramidType::PYR_NORM, 0);
        if (volumeType == VolumeType::ColorTSDF)
            odf.getPyramidAt(colors, OdometryFramePyramidType::PYR_IMAGE, 0);
    }

    patchNaNs(points);
    profile = counterOfValid(points);

    if (cvtest::debugLevel > 0)
    {
        if (volumeType == VolumeType::ColorTSDF)
            displayColorImage(depth, rgb, points, normals, colors, depthFactor, lightPose);
        else
            displayImage(depth, points, normals, depthFactor, lightPose);
    }

    // TODO: why profile == 2*anfas ?
    float percentValidity = float(anfas) / float(profile);

    ASSERT_NE(profile, 0) << "There is no points in profile";
    ASSERT_NE(anfas, 0) << "There is no points in anfas";
    ASSERT_LT(abs(0.5 - percentValidity), 0.3) << "percentValidity out of [0.3; 0.7] (percentValidity=" << percentValidity << ")";
}

void valid_points_test_common_framesize(VolumeType volumeType, VolumeTestSrcType testSrcType)
{
    VolumeSettings vs(volumeType);
    Volume volume(volumeType, vs);

    Size frameSize(vs.getRaycastWidth(), vs.getRaycastHeight());
    Matx33f intr;
    vs.getCameraIntegrateIntrinsics(intr);
    bool onlySemisphere = true;
    float depthFactor = vs.getDepthFactor();
    Vec3f lightPose = Vec3f::all(0.f);
    Ptr<Scene> scene = Scene::create(frameSize, intr, depthFactor, onlySemisphere);
    std::vector<Affine3f> poses = scene->getPoses();

    Mat depth = scene->depth(poses[0]);
    Mat rgb = scene->rgb(poses[0]);
    Mat points, normals, colors, newPoints, newNormals;
    int anfas, profile;

    OdometryFrame odf;
    odf.setDepth(depth);
    odf.setImage(rgb);

    if (testSrcType == VolumeTestSrcType::MAT)
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.integrate(depth, rgb, poses[0].matrix);
        else
            volume.integrate(depth, poses[0].matrix);
    }
    else
    {
        volume.integrate(odf, poses[0].matrix);
    }

    if (testSrcType == VolumeTestSrcType::MAT) // Odometry frame or Mats
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.raycast(poses[0].matrix, points, normals, colors);
        else
            volume.raycast(poses[0].matrix, points, normals);
    }
    else if (testSrcType == VolumeTestSrcType::ODOMETRY_FRAME)
    {
        volume.raycast(poses[0].matrix, odf);
        odf.getPyramidAt(points, OdometryFramePyramidType::PYR_CLOUD, 0);
        odf.getPyramidAt(normals, OdometryFramePyramidType::PYR_NORM, 0);
        if (volumeType == VolumeType::ColorTSDF)
            odf.getPyramidAt(colors, OdometryFramePyramidType::PYR_IMAGE, 0);
    }

    patchNaNs(points);
    anfas = counterOfValid(points);

    if (cvtest::debugLevel > 0)
    {
        if (volumeType == VolumeType::ColorTSDF)
            displayColorImage(depth, rgb, points, normals, colors, depthFactor, lightPose);
        else
            displayImage(depth, points, normals, depthFactor, lightPose);
    }

    points.release();
    normals.release();

    if (testSrcType == VolumeTestSrcType::MAT) // Odometry frame or Mats
    {
        if (volumeType == VolumeType::ColorTSDF)
            volume.raycast(poses[17].matrix, points, normals, colors);
        else
            volume.raycast(poses[17].matrix, points, normals);
    }
    else if (testSrcType == VolumeTestSrcType::ODOMETRY_FRAME)
    {
        volume.raycast(poses[17].matrix, odf);
        odf.getPyramidAt(points, OdometryFramePyramidType::PYR_CLOUD, 0);
        odf.getPyramidAt(normals, OdometryFramePyramidType::PYR_NORM, 0);
        if (volumeType == VolumeType::ColorTSDF)
            odf.getPyramidAt(colors, OdometryFramePyramidType::PYR_IMAGE, 0);
    }

    patchNaNs(points);
    profile = counterOfValid(points);

    if (cvtest::debugLevel > 0)
    {
        if (volumeType == VolumeType::ColorTSDF)
            displayColorImage(depth, rgb, points, normals, colors, depthFactor, lightPose);
        else
            displayImage(depth, points, normals, depthFactor, lightPose);
    }

    // TODO: why profile == 2*anfas ?
    float percentValidity = float(anfas) / float(profile);

    ASSERT_NE(profile, 0) << "There is no points in profile";
    ASSERT_NE(anfas, 0) << "There is no points in anfas";
    ASSERT_LT(abs(0.5 - percentValidity), 0.3) << "percentValidity out of [0.3; 0.7] (percentValidity=" << percentValidity << ")";
}


#ifndef HAVE_OPENCL
TEST(TSDF, raycast_custom_framesize_normals_mat)
{
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
}

TEST(TSDF, raycast_custom_framesize_normals_frame)
{
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(TSDF, raycast_common_framesize_normals_mat)
{
    normal_test_common_framesize(VolumeType::TSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
}

TEST(TSDF, raycast_common_framesize_normals_frame)
{
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(TSDF, fetch_points_normals)
{
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::FETCH_POINTS_NORMALS, VolumeTestSrcType::MAT);
}

TEST(TSDF, fetch_normals)
{
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::FETCH_NORMALS, VolumeTestSrcType::MAT);
}

TEST(TSDF, valid_points_custom_framesize_mat)
{
    valid_points_test_custom_framesize(VolumeType::TSDF, VolumeTestSrcType::MAT);
}

TEST(TSDF, valid_points_custom_framesize_frame)
{
    valid_points_test_custom_framesize(VolumeType::TSDF, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(TSDF, valid_points_common_framesize_mat)
{
    valid_points_test_common_framesize(VolumeType::TSDF, VolumeTestSrcType::MAT);
}

TEST(TSDF, valid_points_common_framesize_frame)
{
    valid_points_test_common_framesize(VolumeType::TSDF, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(HashTSDF, raycast_custom_framesize_normals_mat)
{
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
}

TEST(HashTSDF, raycast_custom_framesize_normals_frame)
{
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(HashTSDF, raycast_common_framesize_normals_mat)
{
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
}

TEST(HashTSDF, raycast_common_framesize_normals_frame)
{
    normal_test_common_framesize(VolumeType::HashTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(HashTSDF, fetch_points_normals)
{
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::FETCH_POINTS_NORMALS, VolumeTestSrcType::MAT);
}

TEST(HashTSDF, fetch_normals)
{
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::FETCH_NORMALS, VolumeTestSrcType::MAT);
}

TEST(HashTSDF, valid_points_custom_framesize_mat)
{
    valid_points_test_custom_framesize(VolumeType::HashTSDF, VolumeTestSrcType::MAT);
}

TEST(HashTSDF, valid_points_custom_framesize_frame)
{
    valid_points_test_custom_framesize(VolumeType::HashTSDF, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(HashTSDF, valid_points_common_framesize_mat)
{
    valid_points_test_common_framesize(VolumeType::HashTSDF, VolumeTestSrcType::MAT);
}

TEST(HashTSDF, valid_points_common_framesize_frame)
{
    valid_points_test_common_framesize(VolumeType::HashTSDF, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(ColorTSDF, raycast_custom_framesize_normals_mat)
{
    normal_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
}

TEST(ColorTSDF, raycast_custom_framesize_normals_frame)
{
    normal_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(ColorTSDF, raycast_common_framesize_normals_mat)
{
    normal_test_common_framesize(VolumeType::ColorTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
}

TEST(ColorTSDF, raycast_common_framesize_normals_frame)
{
    normal_test_common_framesize(VolumeType::ColorTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(ColorTSDF, fetch_normals)
{
    normal_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestFunction::FETCH_NORMALS, VolumeTestSrcType::MAT);
}

TEST(ColorTSDF, fetch_points_normals)
{
    normal_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestFunction::FETCH_POINTS_NORMALS, VolumeTestSrcType::MAT);
}

TEST(ColorTSDF, valid_points_custom_framesize_mat)
{
    valid_points_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestSrcType::MAT);
}

TEST(ColorTSDF, valid_points_custom_framesize_fetch)
{
    valid_points_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestSrcType::ODOMETRY_FRAME);
}

TEST(ColorTSDF, valid_points_common_framesize_mat)
{
    valid_points_test_common_framesize(VolumeType::ColorTSDF, VolumeTestSrcType::MAT);
}

TEST(ColorTSDF, valid_points_common_framesize_fetch)
{
    valid_points_test_common_framesize(VolumeType::ColorTSDF, VolumeTestSrcType::ODOMETRY_FRAME);
}

#else
TEST(TSDF_CPU, raycast_custom_framesize_normals_mat)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, raycast_custom_framesize_normals_frame)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, raycast_common_framesize_normals_mat)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_common_framesize(VolumeType::TSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, raycast_common_framesize_normals_frame)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, fetch_points_normals)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::FETCH_POINTS_NORMALS, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, fetch_normals)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::TSDF, VolumeTestFunction::FETCH_NORMALS, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, valid_points_custom_framesize_mat)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_custom_framesize(VolumeType::TSDF, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, valid_points_custom_framesize_frame)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_custom_framesize(VolumeType::TSDF, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, valid_points_common_framesize_mat)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_common_framesize(VolumeType::TSDF, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(TSDF_CPU, valid_points_common_framesize_frame)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_common_framesize(VolumeType::TSDF, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, raycast_custom_framesize_normals_mat)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, raycast_custom_framesize_normals_frame)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, raycast_common_framesize_normals_mat)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, raycast_common_framesize_normals_frame)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_common_framesize(VolumeType::HashTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, fetch_points_normals)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::FETCH_POINTS_NORMALS, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, fetch_normals)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::HashTSDF, VolumeTestFunction::FETCH_NORMALS, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, valid_points_custom_framesize_mat)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_custom_framesize(VolumeType::HashTSDF, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, valid_points_custom_framesize_frame)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_custom_framesize(VolumeType::HashTSDF, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, valid_points_common_framesize_mat)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_common_framesize(VolumeType::HashTSDF, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(HashTSDF_CPU, valid_points_common_framesize_frame)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_common_framesize(VolumeType::HashTSDF, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, raycast_custom_framesize_normals_mat)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, raycast_custom_framesize_normals_frame)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, raycast_common_framesize_normals_mat)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_common_framesize(VolumeType::ColorTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, raycast_common_framesize_normals_frame)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_common_framesize(VolumeType::ColorTSDF, VolumeTestFunction::RAYCAST, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, fetch_normals)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestFunction::FETCH_NORMALS, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, fetch_points_normals)
{
    cv::ocl::setUseOpenCL(false);
    normal_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestFunction::FETCH_POINTS_NORMALS, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, valid_points_custom_framesize_mat)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, valid_points_custom_framesize_fetch)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_custom_framesize(VolumeType::ColorTSDF, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, valid_points_common_framesize_mat)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_common_framesize(VolumeType::ColorTSDF, VolumeTestSrcType::MAT);
    cv::ocl::setUseOpenCL(true);
}

TEST(ColorTSDF_CPU, valid_points_common_framesize_fetch)
{
    cv::ocl::setUseOpenCL(false);
    valid_points_test_common_framesize(VolumeType::ColorTSDF, VolumeTestSrcType::ODOMETRY_FRAME);
    cv::ocl::setUseOpenCL(true);
}

#endif
}
}  // namespace
