#pragma once

class Work {
private:
	HANDLE hThread;
private:
	void working(const char *);
public:
	Work();
	~Work ();
	void TryWorking(const char *);
};