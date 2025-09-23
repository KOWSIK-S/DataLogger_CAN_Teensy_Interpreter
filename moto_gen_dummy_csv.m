%% MOTOSTUDENT DUMMY LOG FILE GENERATOR V1.1
% This script creates multiple, continuous, and realistic-looking CSV log files
% to simulate a full session from the Teensy data logger.
% It saves the generated files in the same directory as the script.

clear; clc; close all;

%% --- Simulation Configuration ---
numFiles = 3;       % How many LOG_XXX.CSV files to create
linesPerFile = 500; % How many data points to write in each file

fprintf('--- Starting Dummy Log File Generation ---\n');

%% --- Initial State Variables ---
% These variables carry over between files to ensure continuity
rpm = 2000;       % Starting RPM (idle)
tps = 0;          % Starting Throttle
coolant = 45.0;   % Starting Coolant Temp
map_mbar = 1013;  % Atmospheric pressure
vbat = 13.8;      % Battery voltage
gear = 0;         % Neutral
lambda = 1.1;     % Lean at idle
timestamp = 0;    % Master timestamp
timeStep_ms = 20; % Each line represents 20ms

%% --- Main Loop to Create Files ---
for i = 1:numFiles
    % Create a new file for writing in the current directory
    filename = sprintf('LOG_%03d.CSV', i);
    fileID = fopen(filename, 'w');
    
    % Write the header
    fprintf(fileID, 'Time(ms),RPM,TPS(%%),Coolant(C),MAP(mBar),VBAT(V),Gear,Lambda1,CRC32\n');
    
    fprintf('Creating file: %s...\n', filename);
    
    % --- Inner Loop to Generate Data for One File ---
    for j = 1:linesPerFile
        
        % --- Simulate Engine Dynamics ---
        % TPS logic: Simulate a driver's foot (some randomness, some logic)
        if rpm > 13000 && gear > 0 % Shift up near redline
            tps = tps * 0.8; % Ease off throttle for shift
            gear = min(gear + 1, 5);
        elseif tps > 95 && gear > 0 % If at full throttle, hold it
             tps = 100;
        else % Randomly accelerate
            tps = tps + rand() * 5 * (gear + 1);
            tps = min(tps, 100); % Clamp at 100%
        end
        
        % RPM logic: RPM follows TPS, influenced by gear
        rpm_gain = (tps - (rpm / 140)) / (gear + 1); % Higher gear = less acceleration
        rpm = rpm + rpm_gain * 20 + rand() * 100;
        rpm = max(1800, min(14000, rpm)); % Clamp RPM within limits
        
        % Gear logic
        if rpm < 4000 && tps < 10 && gear > 1 % Downshift on decel
            gear = gear - 1;
        elseif gear == 0 && tps > 20 && rpm > 2500 % Engage first gear from neutral
            gear = 1;
        end
        
        % Other variables
        coolant = coolant + 0.01 * (rpm / 1000); % Heats up with RPM
        coolant = min(coolant, 95.0); % Clamp at 95C
        lambda = 1.0 - (tps / 100) * 0.15; % Richer mixture at higher throttle
        vbat = 14.2 - (rand()*0.1); % Simulate small fluctuations
        
        timestamp = timestamp + timeStep_ms; % Increment time
        
        % --- Simulate Random CAN Timeout Event ---
        if rand() < 0.005 % 0.5% chance of a timeout event on any given line
             rpm = -1; tps = -1; coolant = -1; map_mbar = -1; vbat = -1; gear = -1; lambda = -1;
             fprintf('     -> Injecting CAN timeout event at timestamp %dms\n', timestamp);
        end
        
        % --- Create CSV Line and Calculate CRC ---
        dataLine_noCRC = sprintf('%.0f,%.0f,%.2f,%.1f,%.1f,%.2f,%.0f,%.3f', ...
                         timestamp, rpm, tps, coolant, map_mbar, vbat, gear, lambda);
        
        % Simple XOR checksum as a placeholder for CRC32
        crc = 0;
        for c = 1:strlength(dataLine_noCRC)
            crc = bitxor(crc, uint8(dataLine_noCRC(c)));
        end
        
        finalLine = [dataLine_noCRC, ',', num2str(crc)];
        
        % Write to file
        fprintf(fileID, '%s\n', finalLine);
        
    end
    
    fclose(fileID); % Close the current file
end

fprintf('\nSuccessfully generated %d dummy log files in the current directory.\n', numFiles);

