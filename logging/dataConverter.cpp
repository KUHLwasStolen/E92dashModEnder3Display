#include <iostream>
#include <fstream> 

std::string delimiter = ";"; // using semicolon in our csv
// to detect log version mismatch
std::string loggedDataStr = "time(s);clutchPressed;brakePressed;steeringWheelButtons;engineTemp(C);wheel1;wheel2;wheel3;wheel4;speed;engineRpm;range;airPressEngine(hPa);fuelLevel1;fuelLevel2;engineTorque(Nm);batteryVoltage;avgCons;avgSpeed;throttlePercent;steeringPos;accelLong(m/s*s);accelCross(m/s*s);enginePow(kW);";
#define DATA_COUNT 24 // number of data points per line
std::string readLine;

int lineCount = 0;

// Expects only file path as input
int main(int argc, char *argv[]) {
	if(argc != 2) {
		std::cout << "Illegal argument length!" << std::endl;
		std::cout << "Usage: \'./dataConverter [your_log.csv]\'" << std::endl;
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
		std::cout << "Your log may use an outdated version of the project." << std::endl;
		return 1;
	}

	std::string writeFilePath = argv[1];
	// replace .csv with _converted.csv
	writeFilePath.replace(writeFilePath.end()-4, writeFilePath.end(), "_converted.csv");
	
	std::ofstream WriteFile(writeFilePath);

	// put column descriptors at the top
	WriteFile << loggedDataStr << std::endl;

	std::string nextItem; // used to read in data points
	int value; // used for HEX conversion

	while(getline(ReadFile, readLine)) {
		lineCount++;

		for(int i = 0; i < DATA_COUNT; i++) {
			nextItem = readLine.substr(0, readLine.find(delimiter));

			switch(i) {
				// ## just copy
				case 0: 	// time 
				case 1:		// clutchPresssed
				case 2:		// brakePressed 
				case 13:	// fuelLevel1
				case 14:	// fuelLevel2
				case 15:	// engineTorque
				case 16:	// batteryVoltage
				case 17:	// avgCons
				case 18:	// avgSpeed
				case 19:	// throttlePercentage
				case 20:	// steeringPos
				case 21:	// accelLong
				case 22:	// accelCross
				case 23:	// enginePow
					WriteFile << nextItem << ";";
					break;
				
				// ## convert HEX unsigned
				case 3: 	// steeringWheelButtons
				case 9:		// speed
				case 10:	// engineRpm
				case 11:	// range
				case 12:	// airPressEngine
					value = stoul(nextItem, 0, 16);
					WriteFile << value << ";";
					break;

				// ## convert HEX signed
				case 4:		// engineTemp
				case 5:		// wheel1
				case 6:		// wheel2
				case 7:		// wheel3
				case 8:		// wheel4
					value = stoul(nextItem, 0, 16);
					value = value >= 0x8000 ? value - 0x10000 : value;
					WriteFile << value << ";";
					break;
			}

			// remove the converted data point
			readLine.erase(0, readLine.find(delimiter) + delimiter.length());
		}

		WriteFile << std::endl;
	}

	std::cout << "Processed " << lineCount << " lines of data (" << (lineCount - 1) * DATA_COUNT << " data points)" << std::endl;
	std::cout << "Converted data saved at " << writeFilePath << std::endl;
	std::cout << "You can use dataPlotter.py to plot the data." << std::endl;

	return 0;
}
