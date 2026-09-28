
// --------------------------------------------------------------
//
//  Thinkpad Fan Control
//
// --------------------------------------------------------------
//
//	This program and source code is in the public domain.
//
//	The author claims no copyright, copyleft, license or
//	whatsoever for the program itself (with exception of
//	WinIO driver).  You may use, reuse or distribute it's 
//	binaries or source code in any desired way or form,  
//	Useage of binaries or source shall be entirely and 
//	without exception at your own risk. 
// 
// --------------------------------------------------------------

#ifndef FANCONTROL_H
#define FANCONTROL_H

#include "_prec.h"
#pragma once


#include "winstuff.h"
#include "TaskbarTextIcon.h"

// T3-01. The application now includes core headers, so the portable safety
// state machine and the fan-selector refusal rule are no longer dead code that
// the shipped program never sees. The namespace is spelled out at every use
// site rather than opened with a using-directive: a global "using namespace"
// in a header of this size would be the kind of change that silently alters
// name lookup in every file that includes it.
#include "core/app_bridge.h"
#include "core/legacy_backend.h"
#include "core/legacy_policy.h"

#include <memory>
#include <string>
#include <vector>



#define FANCONTROLVERSION "0.63 multiHotKey"

#define WM__DISMISSDLG WM_USER+5
#define WM__GETDATA WM_USER+6
#define WM__NEWDATA WM_USER+7
#define WM__TASKBAR WM_USER+8

#define setzero(adr, size) memset((void*)(adr), (char)0x00, (size))
#define ARRAYMAX(tab) (sizeof(tab)/sizeof((tab)[0]))
#define NULLSTRUCT	{ 0, }

//begin named pipe TPFanControl01
#define g_szPipeName "\\\\.\\Pipe\\TPFanControl01"  //Name given to the pipe
//Pipe name format - \\.\pipe\pipename

#define BUFFER_SIZE 1024 //1k
#define ACK_MESG_RECV "Message received successfully"
//end named pipe TPFanControl01

class FANCONTROL 
{
	protected:
		HINSTANCE hinstapp;
        HINSTANCE m_hinstapp;
		HWND hwndDialog;
        
		UINT_PTR m_fanTimer;
		UINT_PTR m_titleTimer;
		UINT_PTR m_iconTimer;
		UINT_PTR m_renewTimer;

        struct FCSTATE {

			char FanCtrl,
				 FanSpeedLo,
				 FanSpeedHi;

			char Sensors[12];
			int  SensorAddr[12];
			const char *SensorName[12];


		} State;

		struct SMARTENTRY {
				int temp, fan;
		} SmartLevels[32];

		struct SMARTENTRY1 {
				int temp1, fan1;
		} SmartLevels1[32];

		struct SMARTENTRY2 {
				int temp2, fan2;
		} SmartLevels2[32];

		struct FSMARTENTRY {		//fahrenheit values
				int ftemp, ffan;
		} FSmartLevels[32];


		int IconLevels[3];	// temp levels for coloring the icon
		int FIconLevels[3];	// fahrenheit temp levels for coloring the icon
		int CurrentIcon;
		int IndSmartLevel;
		int SensorOffset[16];
		int FSensorOffset[16];
		int iFarbeIconB;
		int iFontIconB;
		int icontemp;
		int Cycle; 
		int IconCycle; 
		int ReIcCycle; 
		int NoExtSensor;
		int FanSpeedLowByte;
		int ActiveMode,
			UseTWR,
			ManFanSpeed,
			FinalSeen;
		int CurrentMode, fanctrl2,
			PreviousMode;
		int TaskbarNew;
		int MaxTemp;
		int iMaxTemp;
		int fanspeed, lastfanspeed, showfanspeed;
		int FanBeepFreq, FanBeepDura;
		int MinimizeToSysTray,
			Lev64Norm,
			IconColorFan,
			Fahrenheit,
			MinimizeOnClose,
			StartMinimized,
			NoWaitMessage,
            Runs_as_service;
		int ReadErrorCount;
		int MaxReadErrors;
		int SecWinUptime;
		int SlimDialog;
		int NoBallons,
			HK_BIOS_Method,
			HK_Manual_Method,
			HK_Smart_Method,
			HK_SM1_Method,
			HK_SM2_Method,
			HK_TG_BS_Method,
			HK_TG_BM_Method,
			HK_TG_MS_Method,
			HK_TG_12_Method,
			HK_BIOS,
			HK_Manual,
			HK_Smart,
			HK_SM1,
			HK_SM2,
			HK_TG_BS,
			HK_TG_BM,
			HK_TG_MS,
			HK_TG_12;
		int BluetoothEDR;
		int ManModeExit;
		int ManModeExit2;
		int ShowBiasedTemps;
		int SecStartDelay;
		char gSensorNames[17][4];
		int Log2File;
		int Log2csv;
		int StayOnTop;
		int ShowAll;
		int ShowTempIcon;
		char IgnoreSensors[256];
		char MenuLabelSM1[32];
		char MenuLabelSM2[32];
		HANDLE hThread;
		HANDLE hPipe0;
		HANDLE hPipe1;
		HANDLE hPipe2;
		HANDLE hPipe3;
		HANDLE hPipe4;
		HANDLE hPipe5;
		HANDLE hPipe6;
		HANDLE hPipe7;
		HANDLE hLock;
		HANDLE hLockS;
		BOOL Closing;
		MUTEXSEM EcAccess;
		bool m_needClose;

		// T3-01/T3-02/T3-04. The portable core, owned by the application.
		//
		// Held by pointer and created only after the port driver is open,
		// because the backend wraps the driver's read and write primitives and
		// must not outlive them. Created read-only unless Phase 0 has verified
		// this machine; see CoreInit.
		//
		// Every EC access in the application goes through here. That is the
		// point: the legacy code had several paths that each talked to the EC
		// under EcAccess independently, and a change to one did not change the
		// others.
		std::unique_ptr<tpfancontrol::core::LegacyBackend> CoreBackend;
		std::unique_ptr<tpfancontrol::core::AppBridge> CoreBridge;

		// T3-04. The monotonic clock the core runs on. A member rather than a
		// function-local static, and declared before CoreBridge so it is
		// destroyed after it: members are destroyed in reverse declaration
		// order, and a bridge outliving the clock it reads would be a
		// use-after-free on shutdown.
		//
		// SteadyClock, never the wall clock. A wall-clock step backwards during
		// NTP correction must not turn a bounded EC wait into an unbounded one.
		tpfancontrol::core::SteadyClock CoreClock;

		// T5/Phase 0. Whether this machine has a verified hardware report. False
		// everywhere until one exists, and the single thing that decides whether
		// the core may control the fan. See CoreInit.
		bool CoreHardwareVerified = false;

		// The reason code from the core's last decision, for the status line and
		// the trace. Empty means the core has not run yet, which is different
		// from a decision with no reason, so it is a string and not a flag.
		std::string LastCoreReason;

		// T5-04. What the core decided at startup: whether there is an I/O path
		// at all, whether readings are available, whether control is approved,
		// and the two strings the UI shows.
		//
		// It is set by CoreInit and is always valid afterwards, including when
		// CoreInit failed - that is the case it exists for. With no backend the
		// application is in monitor-only mode with nothing to read, and the user
		// has to be told why rather than shown an empty window.
		tpfancontrol::core::StartupAssessment CoreStartup;

		// True when the core may not write any register, which is the case in
		// both monitor-only modes. Never a fault: this machine is being used
		// exactly as intended until a hardware report exists (Phase 0).
		bool CoreMonitorOnly() const noexcept;

		// Protected, and deliberately not called from outside the class: it
		// touches the core member by member, and its caller would then have to
		// keep them in step. StartCore() is the public seam.
		// Builds CoreBackend and CoreBridge against the current EC primitives.
		// Returns false if the port driver is not open, in which case the
		// application runs monitor-only and writes nothing.
		bool CoreInit();
		// Releases them. Called when the port driver closes, so nothing can call
		// through a closed driver.
		void CoreShutdown();
		// T3-04. Maps the currently selected smart table into the core's curve
		// and installs it. Called by CoreInit and after every profile switch,
		// so the curve in force is always the profile the user selected.
		//
		// Does nothing when the core is not built (monitor-only), which is not
		// an error: there is no curve to install and nothing may command the fan
		// anyway.
		void ApplySmartLevelsToCore();

		// The application's sensor names in register order, 0x78 first then
		// 0xC0. Returns exactly kSensorCount entries, with empty names for
		// sensors this machine does not have.
		std::vector<std::string> CoreSensorNames() const;

		char Title[128];
		char Title2[128];
		char Title3[128];
		char Title4[128];
		char Title5[128];
		char LastTitle[128];
		char LastTooltip[128];
		char CurrentStatus[256];
		char CurrentStatuscsv[256];

		// dialog.cpp
		int CurrentModeFromDialog();
		int ShowAllFromDialog();
		void ModeToDialog(int mode);
		void ShowAllToDialog(int mode);
		ULONG DlgProc(HWND hwnd, ULONG msg, WPARAM mp1, LPARAM mp2);
		static ULONG CALLBACK BaseDlgProc(HWND hwnd, ULONG msg, WPARAM mp1, LPARAM mp2);

        //The default app-icon with changing colors
		TASKBARICON *pTaskbarIcon;
        //
        CTaskbarTextIcon **ppTbTextIcon;
        MUTEXSEM *pTextIconMutex;



		// The parameter must be LPVOID, not ULONG: on x64 a 32-bit parameter
		// truncates the pointer and the worker dereferences garbage.
		static unsigned __stdcall FANCONTROL_Thread(LPVOID parm) \
                        { return (unsigned)((FANCONTROL *)parm)->WorkThread(); }

		int WorkThread();


		// fancontrol.cpp
		int ReadEcStatus(FCSTATE *pfcstate);
		int ReadEcRaw(FCSTATE *pfcstate);
		int HandleData();
		int SmartControl();
		// T3-11. The target fan is a parameter rather than something SetFan
		// works out for itself, and the only writable value is
		// FirmwareSelected. The legacy signature took a `const char *source` and
		// compared it by pointer identity, which is undefined behaviour unless
		// every argument is a literal in this translation unit; it worked only by
		// accident. An enum cannot be compared wrongly.
		//
		// SetFan does not write any register itself. It asks the portable core
		// for an authorised command and applies it, so there is exactly one place
		// where a fan level reaches the EC and exactly one decision path.
		int SetFan(
			tpfancontrol::core::LegacySource source,
			int level,
			tpfancontrol::core::LegacyFanTarget target
				= tpfancontrol::core::LegacyFanTarget::FirmwareSelected,
			BOOL final= false);
		int SetHdw(const char *source, int hdwctrl, int HdwOffset, int AnyWayBit);


		// misc.cpp
		int ReadConfig(const char *filename);
		void Trace(const char *text);
		void Tracecsv(const char *textcsv);
		void Tracecsvod(const char *textcsv);
		BOOL IsMinimized(void);
		void CurrentDateTimeLocalized(char *result, size_t sizeof_result);
		void CurrentTimeLocalized(char *result, size_t sizeof_result);
		HANDLE CreateThread(unsigned (__stdcall *fnct)(LPVOID), LPVOID p);


		// portio.cpp
		int ReadByteFromECint(int offset, char *pdata);
		int ReadByteFromEC(int offset, char *pdata);
		int WriteByteToEC(int offset, char data);

	public:

		FANCONTROL(HINSTANCE hinstapp);
		~FANCONTROL();

		void Test(void);

		int ProcessDialog();

        // T5-04. Bring the portable core up against the port driver that is
        // open, and record what it decided.
        //
        // Public, and this is the point: CoreInit existed for a whole task
        // without a caller, because it sits in the protected section and the
        // startup path (approot.cpp) is not a member function. Nothing in the
        // tree could tell that the core was never constructed in the shipped
        // application - every fan request simply answered "core not
        // initialised".
        //
        // Returns false when the core could not be built, in which case the
        // application is monitor-only and writes nothing. Never called before
        // the port driver is open.
        bool StartCore();

        // T5-04. What the core decided at startup. Valid after StartCore(),
        // including when it failed - that is the case it exists for.
        const tpfancontrol::core::StartupAssessment& CoreStatus() const noexcept;

        HWND GetDialogWnd() { return hwndDialog; }
		HANDLE GetWorkThread() { return hThread; }
        // The texticons will be shown depending on variables
        void ProcessTextIcons(void);
        void RemoveTextIcons(void);
};

#endif // FANCONTROL_H