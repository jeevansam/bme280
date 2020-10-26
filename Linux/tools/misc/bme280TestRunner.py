import time

# define the name of the file to read from
filename = "/dev/bme280_misc"

# open the file for reading
filehandle = open(filename, 'r')

while True:
    # read a single line
    line = filehandle.readline()
    if not line:
        break
    print(line)
    time.sleep(1)

# close the pointer to that file
filehandle.close()

