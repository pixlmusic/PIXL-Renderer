#include "Modules/ImageReconstruction/CameraReprojection.h"
#include "Modules/ImageReconstruction/GuideFrame.h"
#include <iostream>
#include <stdexcept>

using namespace PIXL::Reconstruction;
using Point = std::array<double, 4>;
static void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static Point Transform(const Point& point, const CameraMatrix& matrix)
{
    Point result{};
    for (size_t i = 0; i < 4; ++i) for (size_t j = 0; j < 4; ++j) result[j] += point[i] * matrix[i * 4 + j];
    return result;
}
static void EqualProjection(const Point& actual, const Point& expected)
{
    for (size_t i = 0; i < 3; ++i)
        Require(std::abs(actual[i] / actual[3] - expected[i] / expected[3]) < 0.001, "projected position mismatch");
}
int main()
{
    try {
        CameraFrame frame;
        frame.reset = false;
        frame.projection = {1.3f,0,0,0, 0,1.7f,0,0, 0,0,1.0001f,1, 0,0,-1.0001f,0};
        frame.currentVP = frame.projection;
        frame.previousVP = frame.projection;
        size_t cases = 0;
        for (float origin : {0.f, 100000.f, -1000000.f, 10000000.f}) {
            for (float delta : {0.f, 1.f, -128.f, 2048.f}) {
                frame.origin = {origin, -origin, origin};
                frame.previousOrigin = {origin - delta, -origin + delta, origin - delta};
                CameraReprojection a, b;
                Require(BuildCameraReprojection(frame, a) && BuildCameraReprojection(frame, b), "valid camera rejected");
                Require(a.clipToPrevious == b.clipToPrevious && a.previousToClip == b.previousToClip, "call order affects frame");
                for (Point point : {Point{10,20,5000,1}, Point{-500,200,15000,1}}) {
                    auto previousPoint = point;
                    for (size_t i = 0; i < 3; ++i) previousPoint[i] += static_cast<double>(frame.origin[i]) - frame.previousOrigin[i];
                    const auto currentClip = Transform(point, frame.currentVP);
                    const auto previousClip = Transform(previousPoint, frame.previousVP);
                    EqualProjection(Transform(currentClip, a.clipToPrevious), previousClip);
                    EqualProjection(Transform(previousClip, a.previousToClip), currentClip);
                    ++cases;
                }
            }
        }
        // Rotation and changed FOV must agree with independently projected points,
        // including when the two frames use different camera-relative origins.
        frame.origin = {10000000.f, -10000000.f, 10000000.f};
        frame.previousOrigin = {9999984.f, -10000008.f, 10000016.f};
        const auto projection = frame.projection;
        for (float angle : {-0.35f, 0.f, 0.35f}) {
            const float c = std::cos(angle), s = std::sin(angle);
            CameraMatrix rotation{c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1};
            frame.currentVP = {};
            for (size_t row=0;row<4;++row) for (size_t col=0;col<4;++col) for (size_t k=0;k<4;++k)
                frame.currentVP[row*4+col] += rotation[row*4+k] * projection[k*4+col];
            frame.previousVP = projection;
            frame.previousVP[0] *= 0.8f;
            frame.previousVP[5] *= 0.8f;
            CameraReprojection result;
            Require(BuildCameraReprojection(frame, result), "rotated camera rejected");
            Point point{50,30,12000,1}, previousPoint=point;
            for (size_t i=0;i<3;++i) previousPoint[i] += static_cast<double>(frame.origin[i])-frame.previousOrigin[i];
            const auto currentClip = Transform(point,frame.currentVP), previousClip = Transform(previousPoint,frame.previousVP);
            EqualProjection(Transform(currentClip,result.clipToPrevious),previousClip);
            EqualProjection(Transform(previousClip,result.previousToClip),currentClip);
            ++cases;
        }
        frame.reset = true;
        CameraReprojection reset;
        Require(BuildCameraReprojection(frame, reset), "reset projection failed");
        for (size_t i = 0; i < 16; ++i) Require(reset.clipToPrevious[i] == (i % 5 == 0 ? 1.f : 0.f), "reset must be identity");
        frame.projection = {};
        Require(!BuildCameraReprojection(frame, reset), "singular projection accepted");
        frame.projection[0] = std::numeric_limits<float>::quiet_NaN();
        Require(!BuildCameraReprojection(frame, reset), "invalid projection accepted");
        NeuralGuideFrame guides{42,1280,720};
        Require(guides.IsValidForAllocation(1920,1080), "valid guide rejected");
        Require(!guides.IsValidForAllocation(640,360), "guide larger than allocation accepted");
        Require(!NeuralGuideFrame{}.IsValidForAllocation(1920,1080), "unpublished guides accepted");
        std::cout << cases << " origin-aware projected position pairs and reset/guide cases passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
