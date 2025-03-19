import matplotlib.pyplot as plt
import numpy as np
import csv
import sys

if len(sys.argv) != 3:
    print("Illegal argument length!")
    print("Usage: 'python dataPlotter.py [your_log_converted.csv] [column to plot]'")
    print("   Or: 'python dataPlotter.py [your_log_converted.csv] all")
    print("Example: 'python dataPlotter.py log_2025_03_04_15h_52m_27s_converted.csv 18'")
    exit()
 
X = []
Y = []

# open passed file
with open(sys.argv[1], 'r') as datafile:
    # separate data points
    plotting = csv.reader(datafile, delimiter=';')

    start = 0
    end = 0

    # if user wants to plot all graphs then get how many there are
    if sys.argv[2] == "all":
        start = 1
        for ROWS in plotting:
            end = len(ROWS) - 1
            break
    # else just plot the one graph that was specified
    else:
        start = int(sys.argv[2])
        end = start + 1 

    for i in range(start, end):
        datafile.seek(0) # return to the beginning of the file
        # clear X, Y
        X = []
        Y = []

        for ROWS in plotting:
            X.append(ROWS[0]) # always time as x-axis
            Y.append(ROWS[i]) # column to plot

        plt.title("BMW KCAN data")
        # set plot size depending on how many values there are to plot
        plt.figure().set_figwidth((len(X) * 25) / 4500)

        # get labels from first line in the file
        xLabel = X.pop(0)
        yLabel = Y.pop(0)

        plt.xlabel(xLabel)
        plt.ylabel(yLabel)

        # convert the data to floats, so it is not plotted as strings
        for i in range(len(X)):
            X[i] = float(X[i])

        for i in range(len(Y)):
            Y[i] = float(Y[i])

        plt.plot(X,Y)

        # get and annotate min/max value, expcept for cases where it makes no sense
        if yLabel != "brakePressed" and yLabel != "clutchPressed" and  yLabel != "steeringWheelButtons":
            maxIndex = np.argmax(Y)
            xMax = X[maxIndex]
            yMax = Y[maxIndex]

            minIndex = np.argmin(Y)
            xMin = X[minIndex]
            yMin = Y[minIndex]

            minMaxDiff = yMax - yMin

            plt.annotate(f"X: {xMax}  Y: {yMax}", xy = (xMax, yMax), arrowprops = dict(facecolor = "red"), xytext = (xMax, yMax + (0.25 * minMaxDiff)))

            plt.annotate(f"X: {xMin}  Y: {yMin}", xy = (xMin, yMin), arrowprops = dict(facecolor = "blue"), xytext = (xMin, yMin - (0.25 * minMaxDiff)))

        plotFileName = sys.argv[1].replace(".csv", "_" + yLabel + ".pdf")

        plt.savefig(plotFileName, bbox_inches='tight')

        print("Plot successfully saved at " + plotFileName)

        plt.clf()
        plt.close()
