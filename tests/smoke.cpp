#include <string_view>
#include <cstdio>

int main()
{
	constexpr std::string_view name = "stuntcarracer";
	std::puts("stuntcarracer: build-only smoke; behavioural counts come from stuntcarracer_selftest");
	return name.empty() ? 1 : 0;
}
