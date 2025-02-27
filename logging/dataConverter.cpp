#include <iostream>
#include <fstream> 

std::string delimiter = ";"; // using semicolon in our csv
// to detect log version mismatch
std::string loggedDataStr = "time(s);clutchPressed;brakePressed;steeringWheelButtons;gearAct;engineTemp;oilTemp;wheel1;wheel2;wheel3;wheel4;speed;engineRpm;range;airPressEngine;fuelLevel1;fuelLevel2;engineTorque;batteryVoltage;avgCons;avgSpeed;throttlePercent;steeringPos;accelLong;accelCross;";
#define DATA_COUNT 25 // number of data points per line
std::string readLine;

int lineCount = 0;

// Expects only file path as input
int main(int argc, char *argv[]) {
	if(argc != 2) {
		std::cout << "Illegal argument length!" << std::endl;
		std::cout << "Usage: \'./dataConverter [log.csv]\'" << std::endl;
		std::cout << "Example: \'./dataConverter testData.csv\'" << std::endl;
		return 1;
	}

	std::ifstream ReadFile(argv[1]);
	
	// handle first line
	if(getline(ReadFile, readLine) && readLine.compare(loggedDataStr) == 0) {
		std::cout << "File verified, starting conversion." << std::endl;
		lineCount++;
	} else {
		std::cout << "Invalid file passed!" << std::endl;
		return 1;
	}

	std::string writeFilePath = argv[1];
	// replace .csv with _converted.csv
	writeFilePath.replace(writeFilePath.end()-4, writeFilePath.end(), "_converted.csv");
	
	std::ofstream WriteFile(writeFilePath);

	WriteFile << loggedDataStr << std::endl;

	std::string nextItem;
	int value;
	while(getline(ReadFile, readLine)) {
		lineCount++;

		for(int i = 0; i < DATA_COUNT; i++) {
			nextItem = readLine.substr(0, readLine.find(delimiter));

			switch(i) {
				// ## just copy
				case 0: 	// time 
				case 1:		// clutchPresssed
				case 2:		// brakePressed 
				case 15:	// fuelLevel1
				case 16:	// fuelLevel2
				case 17:	// engineTorque
				case 18:	// batteryVoltage
				case 19:	// avgCons
				case 20:	// avgSpeed
				case 21:	// throttlePercentage
				case 22:	// steeringPos
				case 23:	// accelLong
				case 24:	// accelCross
					WriteFile << nextItem << ";";
					break;
				
				// ## convert HEX unsigned
				case 3: 	// steeringWheelButtons
				case 4:		// gearAct
				case 11:	// speed
				case 12:	// engineRpm
				case 13:	// range
				case 14:	// airPressEngine
					value = stoul(nextItem, 0, 16);
					WriteFile << value << ";";
					break;

				// ## convert HEX signed
				case 5:		// engineTemp
				case 6:		// oilTemp
				case 7:		// wheel1
				case 8:		// wheel2
				case 9:		// wheel3
				case 10:	// wheel4
					value = stoul(nextItem, 0, 16);
					value = value > 0xFFF ? value - 0x10000 : value; 
					WriteFile << value << ";";
					break;
			}

			readLine.erase(0, readLine.find(delimiter) + delimiter.length());
		}

		WriteFile << std::endl;
	}

	std::cout << "Processed " << lineCount << " lines of data (" << (lineCount - 1) * DATA_COUNT << " data points)" << std::endl;
	std::cout << "Converted data saved at " << writeFilePath << std::endl;

	return 0;
}
