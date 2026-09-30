
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

#include "_prec.h"
#include "fancontrol.h"
#include "tools.h"
#include "TVicPort.h"
#include "tvicport_dll.h"

// T3-11. The fan-selector register. It is named here because the register map
// records it, and read-only code may still refer to it, but this build must
// never WRITE it. The legacy SetFan() wrote it four times per attempt, up to
// five attempts, with no check of any kind - and 0x31 is a register whose
// meaning is not established by any measurement this project has taken.
//
// scripts/check_legacy_ec.py fails the build if a write to it reappears.
#define TP_ECOFFSET_FAN_SWITCH	(char)0x31
#define TP_ECOFFSET_FAN1	(char)0x0000
#define TP_ECOFFSET_FAN2	(char)0x0001
#define TP_ECOFFSET_FAN		(char)0x2F	// 1 byte (binary xyzz zzz)
#define TP_ECOFFSET_FANSPEED 	(char)0x84 // 16 bit word, lo/hi byte
#define TP_ECOFFSET_TEMP0    	(char)0x78	// 8 temp sensor bytes from here
#define TP_ECOFFSET_TEMP1    	(char)0xC0 // 4 temp sensor bytes from here




// Thanks for the following to "Thinker" on 
// http://www.thinkwiki.org/wiki/Talk:ACPI_fan_control_script




		


//-------------------------------------------------------------------------
//  switch fan according to settings
//-------------------------------------------------------------------------
int 
FANCONTROL::HandleData(void)
{
	char obuf[256]= "", obuf2[128]="", 
		 templist[256]= "", templist2[512], 
		 manlevel[16]= "", title2[128]= "";
	int i, maxtemp, imaxtemp, ok= 0;


	//
	// determine highest temp.
	// 

	// build a list of sensors to ignore, separated by "|", e.g. "|XC1|BAT|CPU|"
	char what[16], list[128];
	sprintf_s(list,sizeof(list), "|%s|", this->IgnoreSensors);
	for (i= 0; list[i]!='\0'; i++) {
		if (list[i]==',')	
			list[i]= '|';
	}

	maxtemp= 0;
	imaxtemp= 0;
	int senstemp;
	for (i= 0; i<12; i++) {
		sprintf_s(what,sizeof(what), "|%s|", this->State.SensorName[i]); // name (e.g. "|CPU|") to match against list above

		if (this->State.Sensors[i]!=0x80 && this-State.Sensors[i]!=0x00 && strstr(list, what)==0) {
			int isens=this->State.Sensors[i];
			int ioffs=this->SensorOffset[i];

			if (ShowBiasedTemps)
				senstemp=isens;
			else
				senstemp=isens-ioffs;

			if (senstemp < 128){

			maxtemp= __max(senstemp, maxtemp);
			if (maxtemp <= senstemp) imaxtemp=i;  //this->State.SensorName[this->iMaxTemp]
			}
		}
	}

	this->MaxTemp= maxtemp;
	this->iMaxTemp=imaxtemp;



	//
	// update dialog elements
	//

	// title string (for minimized window)
	if(Fahrenheit)
		sprintf_s(title2,sizeof(title2), "%d°F", this->MaxTemp* 9/5 +32);
	else
		sprintf_s(title2,sizeof(title2), "%d°C", this->MaxTemp);


	// display fan state
	int fanctrl= this->State.FanCtrl;
	fanctrl2= fanctrl;



	if (this->SlimDialog == 1){

	sprintf_s(obuf2,sizeof(obuf2), "Fan %d ", fanctrl);
	if (fanctrl & 0x80)	{if (!(SlimDialog && StayOnTop))
		strcat_s(obuf2,sizeof(obuf2), "(= BIOS)");
		strcat_s(title2,sizeof(title2), " Default Fan");
	}
	else {
		if (!(SlimDialog && StayOnTop))
			sprintf_s(obuf2+strlen(obuf2),sizeof(obuf2)-strlen(obuf2), " Non Bios", fanctrl & 0x3F);
		sprintf_s(title2+strlen(title2),sizeof(title2)-strlen(title2), " Fan %d (%s)",
						 fanctrl & 0x3F,
						 this->CurrentModeFromDialog()==2 ? "Smart" : "Fixed");
	}
	}


	else{
	sprintf_s(obuf2,sizeof(obuf2), "0x%02x (", fanctrl);
	if (fanctrl & 0x80)	{
		strcat_s(obuf2,sizeof(obuf2), "BIOS Controlled)");
		strcat_s(title2,sizeof(title2), " Default Fan");
	}
	else {
		sprintf_s(obuf2+strlen(obuf2),sizeof(obuf2)-strlen(obuf2), "Fan Level %d, Non Bios)", fanctrl & 0x3F);
		sprintf_s(title2+strlen(title2),sizeof(title2)-strlen(title2), " Fan %d (%s)",
						 fanctrl & 0x3F,
						 this->CurrentModeFromDialog()==2 ? "Smart" : "Fixed");
	}
	}


	::SetDlgItemText(this->hwndDialog, 8100, obuf2);

	strcpy_s(this->Title2,sizeof(this->Title2), title2);
	

	// display fan speed (experimental, not visible)
	this->lastfanspeed = this->fanspeed;
	this->fanspeed = (this->State.FanSpeedHi << 8) | this->State.FanSpeedLo;

	if (this->fanspeed > 0x1fff) fanspeed = lastfanspeed;
		sprintf_s(obuf2,sizeof(obuf2), "%d RPM", this->fanspeed);

	::SetDlgItemText(this->hwndDialog, 8102, obuf2);



	// display temperature list
	if(Fahrenheit)
		sprintf_s(obuf2,sizeof(obuf2), "%d°F", this->MaxTemp* 9 /5 +32);
	else
		sprintf_s(obuf2,sizeof(obuf2), "%d°C", this->MaxTemp);
	::SetDlgItemText(this->hwndDialog, 8103, obuf2);


	strcpy_s(templist2,sizeof(templist2), "");
	for (i= 0; i<12; i++) {
		int temp= this->State.Sensors[i];

		if (temp < 128 && temp!= 0) 
		{
			if(Fahrenheit)
				sprintf_s(obuf2,sizeof(obuf2), "%d°F", temp* 9 /5 +32);
			else
				sprintf_s(obuf2,sizeof(obuf2), "%d°C", temp);

				if (SlimDialog && StayOnTop)
					sprintf_s(templist2+strlen(templist2), sizeof(templist2)-strlen(templist2), "%d %s %s", i+1,
					this->State.SensorName[i],obuf2);
				else
					sprintf_s(templist2+strlen(templist2), sizeof(templist2)-strlen(templist2),
					"%d %s %s (0x%02x)", i+1, this->State.SensorName[i], obuf2, this->State.SensorAddr[i]);

			strcat_s(templist2,sizeof(templist2), "\r\n");
		}
		else {
			// strcat_s(templist2,sizeof(templist2), "n/a\r\n");
				if (this->ShowAll==1) 
				{
					sprintf_s(obuf2,sizeof(obuf2), "n/a");
                    size_t strlen_templist = strlen_s(templist2,sizeof(templist2));

				if (SlimDialog && StayOnTop)
					sprintf_s(templist2+strlen_templist,sizeof(templist2)-strlen_templist, "%d %s %s", i+1,
					this->State.SensorName[i],
					obuf2);
				else
					sprintf_s(templist2+strlen_templist,sizeof(templist2)-strlen_templist, "%d %s %s (0x%02x)", i+1,
					this->State.SensorName[i],
					obuf2, 
					this->State.SensorAddr[i]);

					strcat_s(templist2,sizeof(templist2), "\r\n");
			    }
			}
	}

	::SetDlgItemText(this->hwndDialog, 8101, templist2);
	this->icontemp= this->State.Sensors[iMaxTemp];


	// compact single line status (combined)
	strcpy_s(templist,sizeof(templist), "");
	if (Fahrenheit){
		for (i= 0; i<12; i++) {
			if (this->State.Sensors[i]< 128) {
				if (this->State.Sensors[i]!=0)	sprintf_s(templist+strlen(templist),sizeof(templist)-strlen(templist), "%d;", this->State.Sensors[i]* 9 /5 +32);
				else sprintf_s(templist+strlen(templist),sizeof(templist)-strlen(templist), "%d;", 0);
			}
			else {
				strcat_s(templist,sizeof(templist), "0;");
			}
		}
	}
	else {
		for (i= 0; i<12; i++) {
			if (this->State.Sensors[i]!=128) {
				sprintf_s(templist+strlen(templist),sizeof(templist)-strlen(templist), "%d; ", this->State.Sensors[i]);
			}
			else {
				strcat_s(templist,sizeof(templist), "0; ");
			}
		}
	}
	templist[strlen(templist)-1]= '\0';
	if (Fahrenheit)
		sprintf_s(CurrentStatus, sizeof(CurrentStatus), "Fan: 0x%02x / Switch: %d°F (%s)", State.FanCtrl, MaxTemp* 9 /5 +32, templist);
	else 
		sprintf_s(CurrentStatus, sizeof(CurrentStatus), "Fan: 0x%02x / Switch: %d°C (%s)", State.FanCtrl, MaxTemp, templist);

	// display fan speed (experimental, not visible)
    // fanspeed= (this->State.FanSpeedHi << 8) | this->State.FanSpeedLo;

		if (fanspeed > 0x1fff) fanspeed = lastfanspeed;
		sprintf_s(obuf2,sizeof(obuf2), "%d", this->fanspeed);

	sprintf_s(CurrentStatuscsv,sizeof(CurrentStatuscsv), "%s %s; %d; %d; ",templist,obuf2,State.FanCtrl,MaxTemp);

	::SetDlgItemText(this->hwndDialog, 8112, this->CurrentStatus);



	//
	// handle fan control according to mode
	//

	this->CurrentModeFromDialog();
	this->ShowAllFromDialog();

	switch (this->CurrentMode) {

		case 1: // Bios Auto
			if (this->PreviousMode != this->CurrentMode) {
					sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Change Mode from ");
					if (this->PreviousMode==1){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "BIOS->");}
					if (this->PreviousMode==2){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Smart->");}
					if (this->PreviousMode==3){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Manual->");}
					if (this->CurrentMode==1){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "BIOS, setting fan speed");}
					if (this->CurrentMode==2){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Smart, recalculate fan speed");}
					if (this->CurrentMode==3){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Manual, setting fan speed");}
                    this->Trace(obuf);
			}

				if (this->State.FanCtrl!=0x080) 
					ok= this->SetFan(tpfancontrol::core::LegacySource::Bios, 0x80);
				break;


		case 2: // Smart
				this->SmartControl();
				break;


		case 3: // fixed manual
			if (this->PreviousMode != this->CurrentMode) {
					sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Change Mode from ");
					if (this->PreviousMode==1){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "BIOS->");}
					if (this->PreviousMode==2){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Smart->");}
					if (this->PreviousMode==3){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Manual->");}
					if (this->CurrentMode==1){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "BIOS, setting fan speed");}
					if (this->CurrentMode==2){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Smart, recalculate fan speed");}
					if (this->CurrentMode==3){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Manual, setting fan speed");}
                    this->Trace(obuf);
				}

				::GetDlgItemText(this->hwndDialog, 8310, manlevel, sizeof(manlevel));

				if (isdigit(manlevel[0]) && atoi(manlevel)>=0 && atoi(manlevel)<=255) {

					if (this->State.FanCtrl!=atoi(manlevel)) {
						ok= this->SetFan(tpfancontrol::core::LegacySource::Manual, atoi(manlevel));
					}
					else 
						ok= true;
				}
				break;
	}

	this->PreviousMode= this->CurrentMode;

	return ok;
}




//-------------------------------------------------------------------------
//  smart fan control depending on temperature
//-------------------------------------------------------------------------
int 
FANCONTROL::SmartControl(void)
{
		int ok= 0;
        char obuf[256]= "";

			if (this->PreviousMode==1){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Change Mode from BIOS->");
				sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Smart, recalculate fan speed");
				this->Trace(obuf);}
			if (this->PreviousMode==3){sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Change Mode from Manual->");
				sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Smart, recalculate fan speed");
				this->Trace(obuf);}

		// T3-04 and T3-05. The level is not computed here any more.
		//
		// What used to be here: two scans of the smart table per cycle, picking
		// a fan value from MaxTemp and writing it to the EC. It is gone, and
		// the reason it can go is that the same table is now the core's curve
		// (built in CoreInit) and the controller makes the decision - with the
		// up/down thresholds, the validation and the "repeated command is not
		// rewritten" rule that the two unscanned loops never had.
		//
		// The old scans had two properties worth naming, because losing them
		// silently would be a regression rather than a cleanup. The first is
		// the `fanctrl > 7` reset: a level left over from manual mode counted as
		// "hot" and the up-scan only accepted a row whose fan was at least that
		// high. The core's curve tracks its own index and its own command, so a
		// level written by a different mode cannot drag it upwards - and the
		// manual path now expires on its own (ADR-014) instead of leaving a
		// value behind. The second is `newfanctrl != State.FanCtrl`: the
		// duplicate-write suppression. AppBridge::apply refuses to rewrite a
		// value that is already there, tested as
		// testRepeatedCommandIsNotRewritten.
		//
		// The request is made every cycle rather than only when something
		// changed, because "has anything changed" is now a question only the
		// core can answer: it knows the curve index, the dwell time and whether
		// the last write was verified. Deciding it here would be a second
		// decision path, which is what this task is removing.
		ok= this->SetFan(tpfancontrol::core::LegacySource::Smart, this->State.FanCtrl);


	return ok;
}



//-------------------------------------------------------------------------
//  set fan state via EC
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
// T5-04: the public startup seam
//-------------------------------------------------------------------------
// CoreInit and the CoreStartup member are protected, because they are the
// class's own state. The application's startup path is a free function, so it
// needs a way in, and this is it: one call that starts the core and traces what
// it decided.
bool
FANCONTROL::StartCore()
{
	const bool started= this->CoreInit();

	if (started)
		this->Trace(this->CoreStartup.statusLine.c_str());
	else
		this->Trace(this->CoreStartup.explanation.c_str());

	return started;
}

const tpfancontrol::core::StartupAssessment&
FANCONTROL::CoreStatus() const noexcept
{
	return this->CoreStartup;
}

//-------------------------------------------------------------------------
// T5-04: whether the core may write anything at all
//-------------------------------------------------------------------------
// True in both monitor-only modes, including the one where there is no
// backend and therefore nothing to read. It is not a fault state: the machine
// is being used exactly as intended until a hardware report exists.
bool
FANCONTROL::CoreMonitorOnly(void) const noexcept
{
	return this->CoreStartup.monitorOnly;
}

//-------------------------------------------------------------------------
//  build the core against the current driver handle
//-------------------------------------------------------------------------
// The backend wraps the driver's own port primitives, not the application's
// register-level EC helpers. The EC protocol itself has exactly one
// implementation in this codebase, and it is EcBus (ADR-020, ADR-023).
//
// T3-01/T3-02: bring up the portable core against the open port driver.
bool
FANCONTROL::CoreInit()
{
	// Release any previous instance first. A bridge built against a previous
	// driver handle would keep that handle alive, and every EC read after a
	// driver reopen would go to a handle the driver no longer recognises.
	CoreShutdown();

	// T5-04. The startup decision comes first, and it is taken whether or not
	// there is a driver: with no backend the application is in monitor-only
	// mode with nothing to read, and the UI has to be able to say that instead
	// of showing a blank pane. assessStartup is the same decision the status
	// line and the refusal messages use, so there is one answer rather than
	// three.
	tpfancontrol::core::LegacyCapabilityInputs startupInputs;
	startupInputs.backendPresent= true;
	startupInputs.driverLoaded= ::IsDriverOpened() != FALSE;
	startupInputs.controlRequestedByUser= (this->ActiveMode != 0);
	startupInputs.hardwareExactMatch= this->CoreHardwareVerified;
	startupInputs.profileVerified= this->CoreHardwareVerified;
	startupInputs.topologyVerified= this->CoreHardwareVerified;
	startupInputs.restoreCapabilityVerified= this->CoreHardwareVerified;
	startupInputs.monitorOnlyForced= !this->CoreHardwareVerified;
	this->CoreStartup= tpfancontrol::core::assessStartup(startupInputs);

	if (!::IsDriverOpened()) {
		// Monitor-only, and not even readings: the temperature sources are EC
		// registers. Nothing is opened, nothing is written, and the fan stays
		// with the firmware. The reason is in CoreStartup.explanation.
		this->Trace(this->CoreStartup.statusLine.c_str());
		this->Trace(this->CoreStartup.explanation.c_str());
		return false;
	}

	// These are the driver's own port primitives - TVicPort's ReadPort and
	// WritePort - and NOT FANCONTROL::ReadByteFromEC / WriteByteToEC.
	//
	// That distinction is the whole point of T3-02. ReadByteFromEC speaks the
	// complete two-phase EC protocol itself, and EcBus speaks it too. Passing a
	// register-offset API into a port-level backend makes every EcBus operation
	// start a second, nested transaction, and a single register read ends up
	// writing registers 0x04 and 0x00 on a machine whose register map is
	// unverified. An earlier version of this file did exactly that.
	//
	// There is now one implementation of the protocol, in EcBus, and this is the
	// layer beneath it. See ADR-023.
	//
	// T5-01: the mapping from the DLL's entry points onto this port layer used
	// to be written out again here, as two lambdas. It is now the same function
	// the TVicPort adapter uses (makeTvicPortPrimitives), so the DLL's functions
	// are described in one place. The driver handle itself stays with
	// approot.cpp: this function opens nothing.
	const tpfancontrol::core::TvicPortApi api = tpfancontrol::app::makeTvicPortApi();
	const tpfancontrol::core::LegacyPortPrimitives primitives =
		tpfancontrol::core::makeTvicPortPrimitives(api);

	CoreBackend.reset(new tpfancontrol::core::LegacyBackend(primitives));
	CoreBackend->setDriverOpen(true);

	// T3-09 / ADR-007 and the Phase 0 obligations.
	//
	// T3-04: the bridge is built with allowRegisterWrites left at its default of
	// false, and CoreHardwareVerified is false, so every Phase 0 verdict in the
	// capability report is "unverified" and the controller will refuse control
	// even if something else asks for it. On top of that, EcBus refuses a
	// register write outright, before any port is touched.
	//
	// There is deliberately no line here that calls
	// setRegisterWritesAllowed(true). It is not commented out, and not set from a
	// configuration file. It does not exist. Adding it is a source change, made in
	// the same commit as the hardware report that justifies it, where a reviewer
	// sees it.
	//
	// An earlier version of this function instead called
	// CoreBackend->makeReadOnly(). That was wrong: the EC protocol writes a
	// command byte to the status port even to READ, so a backend refusing port
	// writes made every read fail and monitor-only operation was broken. The
	// barrier belongs at the register level, which is where it is now.

	tpfancontrol::core::BridgeConfig bridgeConfig;
	// Stated rather than left at the default, because the default is the safety
	// property and a reader should not have to know that.
	bridgeConfig.allowRegisterWrites = false;
	bridgeConfig.singleFanProfile = true;
	bridgeConfig.requireAllSensors = true;
	// The tachometer is not yet enabled: the 0x1FFF ceiling the legacy
	// application used is a candidate value, not a measurement (Phase 0).
	bridgeConfig.controller.rpmSupported = false;

	// T3-07: the sensor names the legacy UI shows. A name that is missing is
	// left empty rather than filled with a placeholder, because a placeholder
	// reads as a real label.
	bridgeConfig.sensorNames = this->CoreSensorNames();

	CoreBridge.reset(new tpfancontrol::core::AppBridge(*CoreBackend, this->CoreClock, bridgeConfig));

	// T3-04/T3-05. The legacy application chose the fan level with a table of
	// (temperature, fan) rows scanned on every data cycle. That decision now
	// belongs to the core's curve, built from the same configured table, so the
	// table is input data instead of a decision procedure - and SmartControl
	// does not compute a level at all any more.
	//
	// This does not enable anything: the curve is a candidate configuration,
	// exactly like the table it came from, and the controller still refuses to
	// issue a command until every Phase 0 verdict holds. A test asserts that
	// with an empty CapabilityReport the mapped curve still produces no command.
	this->ApplySmartLevelsToCore();

	return true;
}

//-------------------------------------------------------------------------
// T3-04: put the selected smart table into the core's curve
//-------------------------------------------------------------------------
// The legacy application selected a profile by copying SmartLevels1 or
// SmartLevels2 over SmartLevels, and there are six places that do it - menu
// items, buttons and the hotkey path. Each of them calls this afterwards.
//
// The rows come from SmartLevels, which the config parser has already converted
// from Fahrenheit to Celsius, and which is terminated by a negative temperature.
// Only the single-fan profile is mapped: the dual-fan tables belong to a
// topology this project has not verified, and the core refuses to address an
// individual fan (ADR-021), so reading them would produce a curve it could not
// act on.
void
FANCONTROL::ApplySmartLevelsToCore(void)
{
	if (!this->CoreBridge)
		return;		// monitor-only: nothing to install, nothing to command

	tpfancontrol::core::LegacyCurveRow curveRows[32];
	int curveRowCount= 0;
	for (int i= 0; i < 32 && this->SmartLevels[i].temp >= 0; i++) {
		curveRows[curveRowCount].temperatureC= this->SmartLevels[i].temp;
		curveRows[curveRowCount].command= this->SmartLevels[i].fan;
		curveRowCount++;
	}

	const tpfancontrol::core::LegacyCurveMapping mapping=
		tpfancontrol::core::curveFromLegacyRows(curveRows, (std::size_t)curveRowCount);
	const tpfancontrol::core::ValidationResult validation=
		this->CoreBridge->setCurve(mapping.config);

	// The table is a user file, so a refused one has to say which row was wrong
	// and why: a log line saying "invalid curve" would leave the user with a
	// fan that does not respond and no way to find out what to change. An
	// invalid curve leaves the controller in monitor-only mode with reason
	// "invalid_curve", which is the safe direction.
	if (validation.valid) {
		char tablebuf[128];
		sprintf_s(tablebuf,sizeof(tablebuf),
			"Core curve set from the smart table: %d row(s)", mapping.rowsUsed);
		this->Trace(tablebuf);
	}
	else {
		this->Trace("Core curve refused; monitor-only until the smart table is fixed:");
		for (const tpfancontrol::core::ValidationError& error : validation.errors) {
			this->Trace(("  " + error.code + ": " + error.message).c_str());
		}
	}
}

//-------------------------------------------------------------------------
// release the core
//-------------------------------------------------------------------------
void
FANCONTROL::CoreShutdown()
{
	// The bridge holds a reference to the backend, so it must go first.
	CoreBridge.reset();
	CoreBackend.reset();
}

//-------------------------------------------------------------------------
// the application's sensor names, in register order
//-------------------------------------------------------------------------
// 0x78 holds eight sensors and 0xC0 holds four. The legacy application keeps
// its own list, and the mapping between the two is a table rather than an
// index calculation, so it is written out here. A machine that reads fewer
// than twelve gets empty names for the rest, which the UI shows as unknown -
// not as a temperature.
std::vector<std::string>
FANCONTROL::CoreSensorNames() const
{
	std::vector<std::string> names;
	names.reserve(tpfancontrol::core::kSensorCount);

	for (int i = 0; i < tpfancontrol::core::kSensorCount; i++) {
		const char *name = nullptr;
		if (i < (int)ARRAYMAX(this->State.SensorName) && this->State.SensorName[i])
			name = this->State.SensorName[i];
		names.push_back(name ? std::string(name) : std::string());
	}

	// BridgeConfig rejects a list of the wrong length rather than padding it,
	// because a shifted name would label one reading with another's name.
	while (names.size() < (size_t)tpfancontrol::core::kSensorCount)
		names.push_back(std::string());

	return names;
}

// T3-11. This function no longer decides anything and no longer addresses a
// register directly. It asks the portable core whether the request is one the
// core is willing to authorise, and applies the result.
//
// What was removed, and why it is not coming back:
//
//   The legacy body wrote 0x31 (fan selector) with 0x00, wrote 0x2F, wrote 0x31
//   with 0x01, wrote 0x2F again, read 0x2F back, wrote 0x31 with 0x00 again,
//   and read 0x2F back - and repeated that whole sequence up to five times. So
//   a single UI click could produce twenty writes to a register whose meaning is
//   not established by any measurement this project has taken, on a machine
//   whose fan topology is unverified. If 0x31 is not the fan selector on this
//   model, those writes corrupt something else in the embedded controller and
//   there is no way to find out which.
//
// What replaces it: 0x2F is written for the fan the firmware has already
// selected, and it is read back. A request to address a specific fan cannot be
// honoured without the 0x31 write, so it is refused and reported, rather than
// applied to whichever fan happened to be selected and reported as success.
//
// The retry loop is unchanged in count and unchanged in timing. What changed is
// that one attempt is now one write plus one readback, so a failure no longer
// multiplies the exposure of an unknown register by the retry count as well.
int 
FANCONTROL::SetFan(
	 tpfancontrol::core::LegacySource source,
	 int fanctrl,
	 tpfancontrol::core::LegacyFanTarget target,
	 BOOL final)
{
	using tpfancontrol::core::LegacyIntent;
	using tpfancontrol::core::LegacyDecision;
	using tpfancontrol::core::evaluateIntent;

	int ok= 0;
	char obuf[256]= "", obuf2[256], datebuf[128];

	if (this->FanBeepFreq && this->FanBeepDura)
		::Beep(this->FanBeepFreq, this->FanBeepDura);

	this->CurrentDateTimeLocalized(datebuf, sizeof(datebuf));
	

	// The smart request is not a level: the level comes from the core's curve.
	// Printing a value here would make the log line name a number that was never
	// requested, which is exactly the sort of quiet lie this rewrite is meant to
	// remove.
	const bool curveRequest= (source == tpfancontrol::core::LegacySource::Smart);
	if (curveRequest)
		sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "%s: curve control requested, ",
			 tpfancontrol::core::toText(source));
	else
		sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "%s: Set fan control to 0x%02x, ",
			 tpfancontrol::core::toText(source), fanctrl & 0xFF);
	sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Result: ");

	LegacyIntent intent;
	intent.source= source;
	intent.target= target;
	// -1 for a curve request: LegacySource::Smart carries no level, and a
	// placeholder that looked like one would end up in a refusal message.
	intent.level= curveRequest ? -1 : fanctrl;
	intent.final= (final != 0);
	// The legacy application's own guard. It is passed on as data so the
	// refusal that comes back says "monitor-only mode" rather than the caller
	// having to special-case it after the fact.
	intent.controlEnabled= (this->ActiveMode != 0);
	// The legacy application has no per-fan UI selection of its own, so the
	// target parameter carries whatever the caller knows. Recorded so a refusal
	// can say whether a specific fan was asked for.
	intent.userSelectedSpecificFan= (target != tpfancontrol::core::LegacyFanTarget::FirmwareSelected);

	const LegacyDecision decision= evaluateIntent(intent, (std::uint64_t)::GetTickCount64());

	if (decision.refused) {
		// A refusal is a correct outcome, not an error. It is reported in the
		// same status field and traced the same way, so the log distinguishes
		// "the core said no" from "the write failed" - which the legacy code,
		// with a single "FAILED!!" for both, did not.
		sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "REFUSED (%s)",
				 tpfancontrol::core::toText(decision.refusal));
		sprintf_s(obuf2,sizeof(obuf2), "%s   (%s)", obuf, datebuf);
		::SetDlgItemText(this->hwndDialog, 8113, obuf2);
		this->Trace(obuf);
		// The full explanation goes to the log. The status line stays one line.
		this->Trace(decision.message);
		
		// A refusal never sets FinalSeen, because nothing was applied.
		if (!final) ::PostMessage(this->hwndDialog, WM__GETDATA, 0, 0);
		return 0;
	}

	if (this->FinalSeen) {
		sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "IGNORED!(final mode already set)");
	}
	else {
		int ok_ecaccess = false;
		for (int i = 0; i < 10; i++){
			if ( ok_ecaccess = this->EcAccess.Lock(100))break;
			else ::Sleep(100);
		}
		if (!ok_ecaccess){
			sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "FAILED!! (no EC mutex)");
			sprintf_s(obuf2,sizeof(obuf2), "%s   (%s)", obuf, datebuf);
			::SetDlgItemText(this->hwndDialog, 8113, obuf2);
			this->Trace(obuf);
			this->Trace("Could not acquire mutex to set fan state");
			return 0;
		}

		char desired = (char)(fanctrl & 0xFF);

		// T3-04. The level is written by the portable core, through
		// AppBridge::apply, which refuses anything the controller did not
		// authorise and verifies the result by readback. The application no
		// longer has a path to the fan register at all.
		//
		// The intent was already checked by evaluateIntent above. What
		// AppBridge::apply adds is the check that cannot be done by reading the
		// request: that the controller, given the machine's actual capabilities,
		// agrees to issue it. On an unverified machine it will not, and no
		// register is touched - which is the answer a user who ticks "control"
		// before Phase 0 should get.
		//
		// The legacy retry loop is gone because the retry belongs with the
		// write now. A retry that re-issued an unauthorised command would defeat
		// the point of refusing one; a retry that re-issued an authorised one is
		// AppBridge's business, not the dialog's.
		if (!this->CoreBridge) {
			// The core is not initialised, so nothing here may command the fan.
			// The status field takes the one-line decision; the paragraph goes
			// to the log. Putting the explanation in the status field would
			// truncate it mid-sentence in a 256-byte buffer, and a truncated
			// safety message is worse than a short one.
			sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "REFUSED (no core: %s)",
				this->CoreStartup.statusLine.c_str());
			this->Trace(this->CoreStartup.explanation.c_str());
			ok= false;
		}
		else {
			tpfancontrol::core::LegacyCapabilityInputs capabilityInputs;
			capabilityInputs.controlRequestedByUser= (this->ActiveMode != 0);
			capabilityInputs.driverLoaded= true;				// CoreInit proved this
			capabilityInputs.backendPresent= true;
			// Phase 0. All four remain unverified, so the report is not eligible
			// and the controller will not authorise a control command. A BIOS
			// restore is still permitted, because handing the fan back to the
			// firmware is the safe direction and must never be blocked by an
			// incomplete verification.
			capabilityInputs.hardwareExactMatch= this->CoreHardwareVerified;
			capabilityInputs.profileVerified= this->CoreHardwareVerified;
			capabilityInputs.topologyVerified= this->CoreHardwareVerified;
			capabilityInputs.restoreCapabilityVerified= this->CoreHardwareVerified;
			capabilityInputs.monitorOnlyForced= !this->CoreHardwareVerified;

			// The application holds EcAccess, and the core's bus is used only
			// while it is held, so the worker thread cannot interleave a read
			// between the core's write and its readback (T3-06).

			const tpfancontrol::core::EcSnapshot snapshot = this->CoreBridge->read(
				this->CoreClock.nowMs());

			tpfancontrol::core::ControllerInput input =
				tpfancontrol::core::AppBridge::makeInput(
					snapshot, decision.input,
					this->CoreClock.nowMs(),
					snapshot.fanSpeedTimestampMs ?
						(this->CoreClock.nowMs() - snapshot.fanSpeedTimestampMs) : 0);

			input.capabilities= tpfancontrol::core::makeCapabilities(capabilityInputs);
			input.nowMs= this->CoreClock.nowMs();

			const tpfancontrol::core::ControllerOutput output =
				this->CoreBridge->controller().update(input);

			this->LastCoreReason = output.reasonCode;

			if (!output.commandMayBeIssued) {
				sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf),
					"REFUSED by core: %s",
					output.reasonCode.empty() ? "no reason given" : output.reasonCode.c_str());
				this->Trace(obuf);
				ok= false;
			}
			else {
				const tpfancontrol::core::ApplyResult applied =
					this->CoreBridge->apply(output);

				if (applied.succeeded && applied.readbackMatched) {
					ok= true;
				}
				else {
					ok= false;
					// Report what actually happened, not a single "FAILED!!". A
					// refused write, a failed write and a failed readback are three
					// different faults with three different causes, and the legacy
					// code called all three the same thing.
					sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf),
						"FAILED!! (%s)",
						applied.reason.empty() ? "write not confirmed by readback" : applied.reason.c_str());
				}
			}

		}

		this->EcAccess.Unlock();

		this->State.FanCtrl= desired;

		if (ok) {
			sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "OK");
			if (final) 
				this->FinalSeen= true;	// prevent further changes when setting final mode
		}
	}

	// display result
	sprintf_s(obuf2,sizeof(obuf2), "%s   (%s)", obuf, datebuf);
	::SetDlgItemText(this->hwndDialog, 8113, obuf2);

	this->Trace(this->CurrentStatus);
	this->Trace(obuf);

	if (!final) ::PostMessage(this->hwndDialog, WM__GETDATA, 0, 0);
	return ok;
}


int
FANCONTROL::SetHdw(const char *source, int hdwctrl, int HdwOffset, int AnyWayBit)
{
	int ok= 0;
	char obuf[256]= "", obuf2[256], datebuf[128];
	char newhdwctrl;
	
	int ok_ecaccess = false;
	for (int i = 0; i < 10; i++){
		if ( ok_ecaccess = this->EcAccess.Lock(100))break;
		else ::Sleep(100);
	}
	if (!ok_ecaccess){
		this->Trace("Could not acquire mutex to write EC register");
		return 0;
	}

	this->CurrentDateTimeLocalized(datebuf, sizeof(datebuf));

        for (int i = 0; i < 5; i++)
        {
		    ok= this->ReadByteFromEC(HdwOffset, &newhdwctrl);
			if (newhdwctrl & hdwctrl){
		    ok= this->WriteByteToEC(HdwOffset, (newhdwctrl-hdwctrl) | AnyWayBit);
			hdwctrl=newhdwctrl-hdwctrl;}
			else{
		    ok= this->WriteByteToEC(HdwOffset, (newhdwctrl+hdwctrl) | AnyWayBit);
			hdwctrl=newhdwctrl+hdwctrl;}

		    ok= this->ReadByteFromEC(HdwOffset, &newhdwctrl);

          if (hdwctrl == newhdwctrl)
                break;

            ::Sleep(300);
        }

		sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "%s: Set EC register 0x%02x to %d, ", source, HdwOffset, hdwctrl);
		sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "Result: ");

		if (hdwctrl == newhdwctrl) {
			sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "OK");
			ok= true;
		}
		else {
			sprintf_s(obuf+strlen(obuf),sizeof(obuf)-strlen(obuf), "COULD NOT SET HARDWARE STATE!!!!");
			ok= false;
		}


	// display result
	sprintf_s(obuf2,sizeof(obuf2), "%s   (%s)", obuf, datebuf);
	::SetDlgItemText(this->hwndDialog, 8113, obuf2);
	this->Trace(obuf);

	this->EcAccess.Unlock();

	return ok;
}
//-------------------------------------------------------------------------
//  read fan and temperatures from embedded controller
//-------------------------------------------------------------------------
int 
FANCONTROL::ReadEcStatus(FCSTATE *pfcstate)
{
	DWORD ok = 0, rc= 0;

	FCSTATE sample;  //Änderung "{0, }" wg. fanspeed transient zero

//	setzero(pfcstate, sizeof(*pfcstate)); //Änderung // wg. fanspeed transient zero

	
	// reading from the EC seems to yield erratic results at times (probably
	// due to collision with other drivers reading from the port).  So try
	// up to three times to read two samples which look oka and have matching
	// value
	
	int ok_ecaccess = false;
	for (int i = 0; i < 10; i++){
		if ( ok_ecaccess = this->EcAccess.Lock(100))break;
		else ::Sleep(100);
	}
	
	if (!ok_ecaccess){
		this->Trace("Could not acquire mutex to read EC status");
		return ok;
	}

    for (int i = 0; i < 3; i++)
    {
        ok = this->ReadEcRaw(&sample);
/*        if (ok)
        {
            for (int j = 0; j < sizeof(sample.Sensors) / sizeof(*(sample.Sensors)); j++)
            {
                if (sample.Sensors[j] > 128)
                {
                    ok = false;
                    break;
                }
            }
        }
 */       
		if (ok)
            break;
        ::Sleep(200);
    }

	this->EcAccess.Unlock();

	if (ok) {
		memcpy(pfcstate, &sample, sizeof(*pfcstate));
	}

	return ok;
}



//-------------------------------------------------------------------------
//  read fan and temperatures from embedded controller
//-------------------------------------------------------------------------
int 
FANCONTROL::ReadEcRaw(FCSTATE *pfcstate)
{
	int i, idxtemp, ok;
//	pfcstate->FanSpeedLo= 0;
//	pfcstate->FanSpeedHi= 0;
	pfcstate->FanCtrl= -1;
	memset(pfcstate->Sensors, 0, sizeof(pfcstate->Sensors));

	// T3-02. The normal display path now reads through AppBridge/EcBus,
	// serialised by the EcAccess lock held by ReadEcStatus. TWR uses its own
	// block protocol below and deliberately remains on the legacy path.
	if (!this->UseTWR && this->CoreBridge) {
		tpfancontrol::core::DisplayReadOptions options;
		options.noExternalSensors= this->NoExtSensor != 0;
		options.showBiasedTemperatures= this->ShowBiasedTemps != 0;
		for (int i = 0; i < 12; ++i)
			options.sensorOffsets[static_cast<std::size_t>(i)]= this->SensorOffset[i];

		const tpfancontrol::core::DisplayRegisterReadResult display =
			this->CoreBridge->readDisplayRegisters(options);
		if (!display.ok) {
			this->Trace("failed to read display registers through the core EC bus");
			return 0;
		}

		pfcstate->FanCtrl= static_cast<char>(display.fanLevel);
		pfcstate->FanSpeedLo= static_cast<char>(display.fanSpeedLow);
		pfcstate->FanSpeedHi= static_cast<char>(display.fanSpeedHigh);
		const std::array<tpfancontrol::core::DisplayTemperature, 12> mapped =
			tpfancontrol::core::mapRegisterDisplayTemperatures(
				display.rawTemperatures, options);
		for (int i = 0; i < 12; ++i) {
			const std::size_t index= static_cast<std::size_t>(i);
			pfcstate->SensorAddr[i]= mapped[index].registerAddress;
			pfcstate->SensorName[i]= (i >= 8 && this->NoExtSensor)
				? "n/a" : this->gSensorNames[i];
			if (mapped[index].available)
				pfcstate->Sensors[i]= static_cast<char>(mapped[index].valueC);
		}
		return 1;
	}

	ok= ReadByteFromEC(TP_ECOFFSET_FAN, &pfcstate->FanCtrl);

	if (ok)
		ok= ReadByteFromEC(TP_ECOFFSET_FANSPEED, &pfcstate->FanSpeedLo);
	if (!ok)
		{
			this->Trace("failed to read FanSpeedLowByte from EC");
		}

	if (ok)
		ok= ReadByteFromEC(TP_ECOFFSET_FANSPEED+1, &pfcstate->FanSpeedHi);
	if (!ok)
		{
			this->Trace("failed to read FanSpeedHighByte from EC");
		}
	if (!this->UseTWR){
	idxtemp= 0;
	for (i= 0; i<8 && ok; i++) {	// temp sensors 0x78 - 0x7f
		ok= ReadByteFromEC(TP_ECOFFSET_TEMP0+i, &pfcstate->Sensors[idxtemp]);
		if (this->ShowBiasedTemps)
			pfcstate->Sensors[idxtemp] = pfcstate->Sensors[idxtemp] - this->SensorOffset[idxtemp];
		if (!ok)
		{
			this->Trace("failed to read TEMP0 byte from EC");
		}
		pfcstate->SensorAddr[idxtemp]= TP_ECOFFSET_TEMP0+i;
		pfcstate->SensorName[idxtemp]= this->gSensorNames[idxtemp];
		idxtemp++;
	}

	for (i= 0; i<4 && ok; i++) {	// temp sensors 0xC0 - 0xC4
		pfcstate->SensorAddr[idxtemp]= TP_ECOFFSET_TEMP1+i;
		pfcstate->SensorName[idxtemp]= "n/a";
		if (!this->NoExtSensor){
			pfcstate->SensorName[idxtemp]= this->gSensorNames[idxtemp];
			ok= ReadByteFromEC(TP_ECOFFSET_TEMP1+i, &pfcstate->Sensors[idxtemp]);
			if (this->ShowBiasedTemps)
				pfcstate->Sensors[idxtemp] = pfcstate->Sensors[idxtemp] - this->SensorOffset[idxtemp];
			if (!ok) {
				this->Trace("failed to read TEMP1 byte from EC");
			}
		}
		idxtemp++;
	}
	}
	else {
char data= -1;
char dataOut [16];
int iOK = false;
int iTimeout = 100;
int iTimeoutBuf = 1000;
int	iTime= 0;
int iTick= 10;
int ivers= 0;

neuerversuch :

ivers++;

if (ivers >= 3 ) {
	this->Trace("failed to read temps , EC is not ready for TWR");
	ok = 0;
	return ok;}

for (iTime = 0; iTime < iTimeoutBuf; iTime+= iTick){	// wait for ec ready
	data = (char)ReadPort(0x1604) & 0xff;				// or timeout iTimeoutBuf = 1000
	if (!data)											// ec is ready: ctrlprt = 0
		break;
	if (data & 0x50)									// some unrequested outputis waiting 
		ReadPort(0x161f);								// clear data output
	::Sleep(iTick);}

	WritePort(0x1610, 0x20);							// tell them we want to read
	data = (char)ReadPort(0x1604) & 0xff;
	if (!(data & 0x20))									// ec is not ready 
		goto neuerversuch;

for (int i = 1; i < 15; i++) {
	WritePort(0x1610 + i, 0x00);}

WritePort(0x161f, 0x00);

for (iTime = 0; iTime < iTimeoutBuf; iTime++) {			// wait for full buffers to clear	
	data = (char)ReadPort(0x1604) & 0xff;				// or timeout iTimeoutBuf = 1000
	if (data == 0x50) 
		break;
}

if (data != 0x50) 
	goto neuerversuch;

for (int i = 0; i < 16; i++) {	
	dataOut[i] = (char)ReadPort(0x1610 + i) & 0xff;}

pfcstate->SensorAddr[0]= 0x78;
pfcstate->SensorName[0]= this->gSensorNames[0];
pfcstate->Sensors[0]= dataOut[0]; 

pfcstate->SensorAddr[1]= 0x79;
pfcstate->SensorName[1]= this->gSensorNames[1];
pfcstate->Sensors[1]= dataOut[1]; 

pfcstate->SensorAddr[2]= 0x7a;
pfcstate->SensorName[2]= this->gSensorNames[2];
pfcstate->Sensors[2]= dataOut[2]; 

pfcstate->SensorAddr[3]= 0x7b;
pfcstate->SensorName[3]= this->gSensorNames[3];
pfcstate->Sensors[3]= dataOut[3]; 

pfcstate->SensorAddr[4]= 0x7c;
pfcstate->SensorName[4]= this->gSensorNames[4]; 
pfcstate->Sensors[4]= dataOut[4]; 

pfcstate->SensorAddr[5]= 0x7d;
pfcstate->SensorName[5]= this->gSensorNames[5];
pfcstate->Sensors[5]= dataOut[6]; 

pfcstate->SensorAddr[6]= 0x7e;
pfcstate->SensorName[6]= this->gSensorNames[6]; 
pfcstate->Sensors[6]= dataOut[8]; 

pfcstate->SensorAddr[7]= 0x7f;
pfcstate->SensorName[7]= this->gSensorNames[7];
pfcstate->Sensors[7]= dataOut[9]; 

pfcstate->SensorAddr[8]= 0xc0;
pfcstate->SensorName[8]= this->gSensorNames[8];
pfcstate->Sensors[8]= dataOut[10]; 

pfcstate->SensorAddr[9]= 0xc1;
pfcstate->SensorName[9]= this->gSensorNames[9];
pfcstate->Sensors[9]= dataOut[11]; 

pfcstate->SensorAddr[10]= 0xc2;
pfcstate->SensorName[10]= this->gSensorNames[10];
pfcstate->Sensors[10]= dataOut[12]; 

pfcstate->SensorAddr[11]= 0xc3;
pfcstate->SensorName[11]= this->gSensorNames[11];
pfcstate->Sensors[11]= dataOut[13]; 
	}
return ok;
}


