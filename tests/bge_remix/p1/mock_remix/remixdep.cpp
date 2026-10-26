/* BGE-Remix P1 test: a dependency of the fake d3d9_x64.dll, found only in
 * the runtime folder (like NRD.dll or nvngx_dlss.dll for the real runtime). */
extern "C" __declspec(dllexport) int remixdep_ping(void)
{
	return 42;
}
