# SleekOcpp

An Ocpp websocket server. It should be able to operate as a GUI app based on Qt and run as a process just logging to a console or a file.
Currently I have this project as a starting point:
Reference: /Users/ev@Zaptec.com/code/pyWs

pyWs works and is useful. But with several chargers connected I lack overview. For the MCVP with I want to replicate pyWs and have a GUI.
I want the application made with C++. A database is probably needed, if so we will use Sqlite.
The GUI should show what chargers are connected with their current status, we get the status from the StatusNotification message.
For a detail view of the charger I want to be able to see its last session and last messages. And also see when it sent the last heartbeat, metervalue, last connection and disconnect.
We should also be able to see the chargers configuration and be able to change the configuration.
We should be able to start the application in either Gui mode or just as a server like pyWs.

pyWs has a rest API, it would be nice to continue having that. But I would like to be able to start and stop charging from the Gui. This will be done sending RemoteStartTransaction and RemoteStopTransaction.
