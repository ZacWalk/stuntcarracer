#include <string_view>

int main()
{
    constexpr std::string_view name = "stuntcarracer";
    return name.empty() ? 1 : 0;
}
