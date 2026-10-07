/*
** Renegade Xbox port: foundation smoke test.
**
** Links the ported foundation libraries and checks, on the real Xbox (or xemu), that they
** behave: strings, wide-character printf, math rounding, CPU and memory detection, timers,
** threads, the settings file that replaces the registry, and reading every file inside
** the game's .mix/.dat archives found next to this program.
**
** Results are printed on screen as PASS / FAIL lines.
*/
#include <hal/debug.h>
#include <hal/video.h>
#include <nxdk/mount.h>
#include <string.h>
#include <mmsystem.h>    // timeGetTime

#include "always.h"
#include "wwstring.h"
#include "widestring.h"
#include "wwmath.h"
#include "cpudetect.h"
#include "thread.h"
#include "registry.h"
#include "xbox_settings_store.h"
#include "ffactory.h"
#include "mixfile.h"
#include "wwfile.h"
#include "crc.h"
#include "vector.h"
#include "systimer.h"

static int g_pass = 0;
static int g_fail = 0;

/* --- Early start-up ------------------------------------------------------------------
** The engine runs code in global constructors before main() (cpudetect measures the CPU
** there). To see how far start-up gets, this runs earlier still: it sits in the C
** initializer list (.CRT$XI*), which the runtime calls before any C++ constructor, the
** same mechanism nxdk uses to mount D:. It sets the video mode, then switches on the
** port's PORT_TRACE markers, so every start-up step is printed as it begins. If the
** program stops, the last line on screen shows where. */

static void __cdecl print_trace(const char *message)
{
	debugPrint("  start-up: %s\n", message);
}

extern "C" {
static int __cdecl smoke_early_init(void)
{
	XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);
	debugPrint("Renegade Xbox port - foundation smoke test\n\n");
	debugPrint("  start-up: early init reached (before C++ constructors)\n");
	XboxPort_Trace = print_trace;
	return 0;
}
__attribute__((section(".CRT$XIU"), used))
int (__cdecl *const smoke_early_init_p)(void) = smoke_early_init;
}

static void result(bool ok, const char *name, const char *detail)
{
	debugPrint("%s  %-10s %s\n", ok ? "PASS" : "FAIL", name, detail ? detail : "");
	if (ok) g_pass++; else g_fail++;
}

/* --- Strings ------------------------------------------------------------------------- */

static void test_strings()
{
	StringClass s;
	s.Format("%s-%d", "Nod", 42);
	result(strcmp(s, "Nod-42") == 0, "string", s);

	WideStringClass w;
	w.Format(L"%s %d%%", L"Havoc", 100);      // MS rules: %s is a wide string here
	StringClass narrow;
	w.Convert_To(narrow);
	result(strcmp(narrow, "Havoc 100%") == 0, "widestring", narrow);
}

/* --- Math ---------------------------------------------------------------------------- */

static void test_math()
{
	// The original x87 code rounds to nearest (ties to even); the port must match.
	long a = WWMath::Float_To_Long(3.7f);
	long b = WWMath::Float_To_Long(-1.6f);
	long c = WWMath::Float_To_Long(2.5f);
	StringClass d;
	d.Format("3.7->%ld  -1.6->%ld  2.5->%ld", a, b, c);
	result(a == 4 && b == -2 && c == 2, "rounding", d);

	float r = WWMath::Inv_Sqrt(4.0f);
	d.Format("1/sqrt(4) = %d/1000", (int)(r * 1000.0f + 0.5f));
	result(r > 0.499f && r < 0.501f, "inv_sqrt", d);
}

/* --- CPU and memory (runs cpudetect's CPUID assembly and the kernel memory query) ----- */

static void test_cpu()
{
	StringClass d;
	d.Format("%s, ~%d MHz", CPUDetectClass::Get_Processor_String(),
	         CPUDetectClass::Get_Processor_Speed());
	result(CPUDetectClass::Get_Processor_String()[0] != 0, "cpu", d);

	unsigned total = CPUDetectClass::Get_Total_Physical_Memory() / (1024 * 1024);
	unsigned avail = CPUDetectClass::Get_Available_Physical_Memory() / (1024 * 1024);
	d.Format("%u MB total, %u MB free", total, avail);
	result(total >= 60 && avail > 0 && avail <= total, "memory", d);
}

/* --- Clock: cpudetect's original measurement, without its raw-byte assembly --------------
** Start-up used to hang in this measurement. Here it runs in main(), timing the CPU cycle
** counter (read with the compiler's built-in) against the engine's timer for 200 ms, with a
** safety limit. If it finishes, the timer works and the hang was the raw-byte RDTSC
** assembly. xemu may not run the counter at exactly 733 MHz, so a different measured speed
** there is information, not a failure. */

static void test_clock()
{
	unsigned long long t0 = __builtin_ia32_rdtsc();
	unsigned long long t1 = t0;
	unsigned long start = TIMEGETTIME();
	unsigned long elapsed = 0;
	bool capped = false;
	while ((elapsed = TIMEGETTIME() - start) < 200) {
		t1 = __builtin_ia32_rdtsc();
		if (t1 - t0 > 5ULL * 733333333ULL) {    // ~5 s of cycles without 200 ms passing
			capped = true;
			break;
		}
	}
	unsigned measured = elapsed ? (unsigned)((t1 - t0) / (elapsed * 1000ULL)) : 0;
	StringClass d;
	d.Format("timer %s, counter %s, measured ~%u MHz (reported %d MHz)",
	         capped ? "STUCK" : "advances", (t1 > t0) ? "advances" : "STUCK",
	         measured, CPUDetectClass::Get_Processor_Speed());
	result(!capped && t1 > t0 && elapsed >= 200, "clock", d);
}

/* --- Timer and threads ----------------------------------------------------------------- */

class CounterThread : public ThreadClass {
public:
	CounterThread() : ThreadClass("smoke counter"), Count(0) {}
	volatile int Count;
protected:
	virtual void Thread_Function()
	{
		while (running) {
			Count++;
			Sleep_Ms(1);
		}
	}
};

static void test_threads()
{
	CounterThread t;
	DWORD start = timeGetTime();
	t.Execute();

	// The thread sets 'running' itself once it starts, so wait for that before stopping it.
	while (!t.Is_Running() && timeGetTime() - start < 1000) Sleep(1);
	bool started = t.Is_Running();

	Sleep(100);
	t.Stop();
	DWORD elapsed = timeGetTime() - start;

	StringClass d;
	d.Format("counted %d in %lu ms, stopped cleanly", (int)t.Count, (unsigned long)elapsed);
	result(started && t.Count > 0 && !t.Is_Running() && elapsed >= 90, "threads", d);
}

/* --- Settings (RegistryClass on the settings file) ------------------------------------- */

static void test_settings()
{
	// E: is the hard disk's data partition; the disc (D:) is read-only.
	if (!nxIsDriveMounted('E')) {
		nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
	}
	XboxSettings::Set_File("E:\\renegade_smoke_settings.dat");

	int run;
	{
		RegistryClass reg("Software\\Westwood\\Renegade\\SmokeTest", true);
		if (!reg.Is_Valid()) {
			result(false, "settings", "could not create key");
			return;
		}
		run = reg.Get_Int("RunCount", 0) + 1;
		reg.Set_Int("RunCount", run);
	}   // RegistryClass writes the file when it goes away

	// The Xbox caches disk writes; the store asks the kernel to write them out after saving.
	int flushed = XboxSettings::Last_Disk_Flush();

	XboxSettings::Reload();   // throw away memory and read the file back from disk
	RegistryClass check("Software\\Westwood\\Renegade\\SmokeTest", false);
	int stored = check.Is_Valid() ? check.Get_Int("RunCount", -1) : -1;

	StringClass d;
	d.Format("run #%d saved and read back (reset: it should go up)", run);
	result(stored == run, "settings", d);

	d.Format("file %s, folder %s",
	         (flushed & XboxSettings::FLUSHED_FILE) ? "written" : "NOT written",
	         (flushed & XboxSettings::FLUSHED_FOLDER) ? "written" : "NOT written");
	result(flushed == (XboxSettings::FLUSHED_FILE | XboxSettings::FLUSHED_FOLDER), "disk flush", d);
}

/* --- Archives: read every file in every .mix/.dat on D: ------------------------------- */

static bool has_archive_extension(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot && (_stricmp(dot, ".mix") == 0 || _stricmp(dot, ".dat") == 0);
}

static void test_one_archive(const char *filename)
{
	StringClass path;
	path.Format("D:\\%s", filename);

	SimpleFileFactoryClass simple;
	MixFileFactoryClass mix(path, &simple);
	if (!mix.Is_Valid()) {
		result(false, filename, "not a valid MIX archive");
		return;
	}

	DynamicVectorClass<StringClass> names;
	mix.Build_Filename_List(names);

	static unsigned char buffer[64 * 1024];
	unsigned long crc = 0;
	unsigned long total = 0;
	int bad = 0;
	DWORD start = timeGetTime();

	for (int i = 0; i < names.Count(); i++) {
		FileClass *file = mix.Get_File(names[i]);
		if (!file || !file->Open()) {
			bad++;
			if (file) mix.Return_File(file);
			continue;
		}
		int size = file->Size();
		int read_total = 0;
		int n;
		while ((n = file->Read(buffer, sizeof(buffer))) > 0) {
			crc = CRC::Memory(buffer, n, crc);
			read_total += n;
		}
		if (read_total != size) bad++;
		total += read_total;
		file->Close();
		mix.Return_File(file);
	}

	DWORD ms = timeGetTime() - start;
	StringClass d;
	d.Format("%d files, %lu KB in %lu ms, crc %08lX%s", names.Count(), total / 1024,
	         (unsigned long)ms, crc, bad ? "  (READ ERRORS)" : "");
	result(names.Count() > 0 && bad == 0, filename, d);
	if (names.Count() > 0) {
		debugPrint("            first file: %s\n", (const char *)names[0]);
	}
}

static void test_archives()
{
	WIN32_FIND_DATAA find;
	HANDLE h = FindFirstFileA("D:\\*", &find);
	int found = 0;
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			    has_archive_extension(find.cFileName)) {
				test_one_archive(find.cFileName);
				found++;
			}
		} while (FindNextFileA(h, &find));
		FindClose(h);
	}
	if (!found) {
		result(false, "archives", "no .mix/.dat files found on D: (see README)");
	}
}

/* --- Main --------------------------------------------------------------------------- */

int main(void)
{
	debugPrint("  start-up: main() reached\n\n");
	XboxPort_Trace = NULL;    // tests print their own results from here on

	test_strings();
	test_math();
	test_cpu();
	test_clock();
	test_threads();
	test_settings();
	test_archives();

	debugPrint("\n%d passed, %d failed\n", g_pass, g_fail);
	while (1) {
		Sleep(1000);
	}
	return 0;
}
