#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace fan::monitor {
constexpr float missing=std::numeric_limits<float>::quiet_NaN();
constexpr std::int64_t interval=10, horizon=24*60*60;
struct Sample {
    std::int64_t time{};
    float cpuLoad{missing}, gpuLoad{missing}, cpuMHz{missing}, gpuMHz{missing};
    float cpuTemp{missing}, gpuTemp{missing}, memoryLoad{missing};
    float memoryUsedGiB{missing}, memoryTotalGiB{missing}, fanRPM{missing};
};
bool valid(float value);
float checked(double value,double low,double high);
float cpuUsage(std::uint64_t idle,std::uint64_t kernel,std::uint64_t user,
               std::uint64_t oldIdle,std::uint64_t oldKernel,std::uint64_t oldUser);
struct Engine { std::wstring name; double load; };
// Sum processes within each physical engine, then select the busiest engine.
float gpuUsage(std::span<const Engine> engines,const std::wstring& luid);
class History {
public:
    static constexpr std::size_t capacity=static_cast<std::size_t>(horizon/interval)+1;
    void append(const Sample& sample);
    const Sample& at(std::size_t index) const;
    std::size_t size() const { return count_; }
    bool load(const std::filesystem::path& file,std::int64_t now);
    bool save(const std::filesystem::path& file) const;
private:
    std::array<Sample,capacity> samples_{};
    std::size_t first_{},count_{};
};
std::int64_t nowSeconds();
}
