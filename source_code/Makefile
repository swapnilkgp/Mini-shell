install:	shell.cpp
	sudo apt-get install libreadline-dev
	g++ -o shell shell.cpp -lreadline

malware:	malware
	g++ -o malware malware.cpp

lock:	lock
	touch lock-test.txt
	g++ -o lock lock.cpp

clean:
	rm -f shell malware