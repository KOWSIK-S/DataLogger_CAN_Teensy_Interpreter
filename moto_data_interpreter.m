%% MOTOTSTUDENT - ADVANCED SESSION ANALYSIS SUITE V4.0
% This script provides a comprehensive engineering analysis of MotoStudent log
% files. It automatically handles session stitching, performs a CRC32 data
% integrity check, and generates advanced, context-aware visualizations
% based on the official KTM Engine and AIM ECU technical documentation.

clear; clc; close all;

%% 1. Select Session Directory
fprintf('--- MotoStudent Advanced Data Analysis ---\n');
folderPath = uigetdir('', 'Select the folder containing your session''s LOG files');
if folderPath == 0
    disp('User selected Cancel. Analysis stopped.');
    return;
end
disp(['Analyzing session data in: ', folderPath]);

logFiles = dir(fullfile(folderPath, 'LOG_*.CSV'));
if isempty(logFiles)
    error('No log files (LOG_*.CSV) found in the selected directory.');
end

%% 2. Sort and Stitch Log Files
fprintf('Found %d log files. Sorting and stitching...\n', length(logFiles));
% --- Sort files by number in the filename for correct chronological order ---
fileNumbers = cellfun(@(s) sscanf(s, 'LOG_%d.CSV'), {logFiles.name});
[~, sortIdx] = sort(fileNumbers);
logFiles = logFiles(sortIdx);

% --- Define consistent import options and stitch files ---
masterLogData = table();
lastTimestamp = 0;
opts = detectImportOptions(fullfile(folderPath, logFiles(1).name));
opts.VariableNamingRule = 'preserve'; % Keep original headers like 'TPS(%)'

for i = 1:length(logFiles)
    currentFile = fullfile(folderPath, logFiles(i).name);
    try
        tempData = readtable(currentFile, opts);
        if ~isempty(tempData)
            % Ensure timestamp is numeric and make continuous
            if ~isnumeric(tempData.("Time(ms)"))
                tempData.("Time(ms)") = str2double(tempData.("Time(ms)"));
            end
            tempData.("Time(ms)") = tempData.("Time(ms)") + lastTimestamp;
            lastTimestamp = tempData.("Time(ms)")(end);
            masterLogData = [masterLogData; tempData];
        end
    catch ME
        fprintf('     WARNING: Could not process file %s. Error: %s. Skipping.\n', logFiles(i).name, ME.message);
    end
end
fprintf('Session stitching complete. Total data points: %d\n', height(masterLogData));

%% 3. Data Integrity Check (CRC Verification)
fprintf('Performing data integrity check...\n');
numRows = height(masterLogData);
invalidCrcCount = 0;
for i = 1:numRows
    row = masterLogData(i, :);
    % Recreate the exact string the logger would have made
    line_no_crc = sprintf('%.0f,%.0f,%.2f,%.1f,%.1f,%.2f,%.0f,%.3f', ...
        row.("Time(ms)"), row.RPM, row.("TPS(%)"), row.("Coolant(C)"), ...
        row.("MAP(mBar)"), row.("VBAT(V)"), row.Gear, row.Lambda1);
    
    % Calculate XOR checksum (matching the Teensy code)
    calculated_crc = 0;
    for c = 1:strlength(line_no_crc)
        calculated_crc = bitxor(calculated_crc, uint8(line_no_crc(c)));
    end
    
    if calculated_crc ~= row.CRC32
        invalidCrcCount = invalidCrcCount + 1;
    end
end
integrityPercentage = (1 - invalidCrcCount / numRows) * 100;
fprintf('Integrity Check Complete: %.2f%% of data is valid. (%d corrupted lines found)\n', integrityPercentage, invalidCrcCount);


%% 4. Data Cleaning and Preparation
logData = masterLogData;
timeoutIndices = logData.RPM == -1;
logData.RPM(timeoutIndices) = NaN;
logData.("TPS(%")(timeoutIndices) = NaN;
logData.("Coolant(C)")(timeoutIndices) = NaN;
logData.Gear(timeoutIndices) = NaN;
logData.Lambda1(timeoutIndices) = NaN;
time_s = logData.("Time(ms)") / 1000;

%% 5. Generate Advanced Visualizations

% --- FIGURE 1: PROFESSIONAL DASHBOARD VIEW WITH CONTEXT ---
figure('Name', 'Session Dashboard', 'NumberTitle', 'off', 'WindowState', 'maximized');
t = tiledlayout(2, 2, 'TileSpacing', 'compact', 'Padding', 'compact');
title(t, 'Race Session Dashboard', 'FontSize', 16, 'FontWeight', 'bold');

% RPM Plot with Engine Limit
ax1 = nexttile;
plot(ax1, time_s, logData.RPM, 'Color', '#F97300', 'LineWidth', 1.5);
hold(ax1, 'on');
yline(ax1, 14000, '--r', 'Max Engine RPM (14,000)', 'LineWidth', 2, 'LabelVerticalAlignment', 'bottom');
title(ax1, 'Engine RPM'); ylabel(ax1, 'RPM'); grid on; hold(ax1, 'off');

% TPS Plot
ax2 = nexttile;
plot(ax2, time_s, logData.("TPS(%)"), 'Color', '#0072BD', 'LineWidth', 1.5);
title(ax2, 'Throttle Position'); ylabel(ax2, 'TPS (%)'); grid on;

% Coolant Temperature Plot with Operating Zones
ax3 = nexttile;
hold(ax3, 'on');
% KTM Engine Spec: Optimal temp is 90C, max is 100C. Assume <80C is cold.
patch(ax3, [time_s(1) time_s(end) time_s(end) time_s(1)], [0 0 80 80], 'b', 'FaceAlpha', 0.1, 'EdgeColor', 'none', 'DisplayName', 'Cold Zone');
patch(ax3, [time_s(1) time_s(end) time_s(end) time_s(1)], [80 80 100 100], 'g', 'FaceAlpha', 0.1, 'EdgeColor', 'none', 'DisplayName', 'Optimal Zone');
patch(ax3, [time_s(1) time_s(end) time_s(end) time_s(1)], [100 100 120 120], 'r', 'FaceAlpha', 0.1, 'EdgeColor', 'none', 'DisplayName', 'Overheat Zone');
plot(ax3, time_s, logData.("Coolant(C)"), 'Color', '#EDB120', 'LineWidth', 2, 'DisplayName', 'Coolant Temp');
title(ax3, 'Coolant Temperature with Operating Zones'); ylabel(ax3, 'Temp (°C)'); xlabel(ax3, 'Time (s)'); grid on; ylim([min(40, min(logData.("Coolant(C)"))-5) max(110, max(logData.("Coolant(C)"))+5)]);
legend(ax3, 'Location', 'northwest');
hold(ax3, 'off');

% Gear Position Plot
ax4 = nexttile;
stairs(ax4, time_s, logData.Gear, 'k-', 'LineWidth', 2);
title(ax4, 'Gear Position'); ylabel(ax4, 'Gear'); xlabel(ax4, 'Time (s)'); ylim([-1 6]); grid on;


% --- FIGURE 2: 3D FUEL MAP WITH TARGET LAMBDA ---
figure('Name', '3D Fuel Map', 'NumberTitle', 'off', 'WindowState', 'maximized');
validFuelIndices = ~isnan(logData.RPM) & ~isnan(logData.("TPS(%)")) & ~isnan(logData.Lambda1);
F = scatteredInterpolant(logData.RPM(validFuelIndices), logData.("TPS(%")(validFuelIndices), logData.Lambda1(validFuelIndices), 'linear', 'none');
[rpmGrid, tpsGrid] = meshgrid(linspace(2000, 14000, 50), linspace(0, 100, 50));
lambdaGrid = F(rpmGrid, tpsGrid);
surf(rpmGrid, tpsGrid, lambdaGrid, 'EdgeColor', 'none', 'FaceAlpha', 0.8);
hold on;
% Plot a transparent plane at the target lambda value
[X,Y] = meshgrid(linspace(2000, 14000, 2), linspace(0, 100, 2));
Z = ones(size(X)) * 0.88; % Target Lambda from KTM docs
surf(X, Y, Z, 'FaceColor', 'g', 'FaceAlpha', 0.3, 'EdgeColor', 'none');
title('Lambda Surface vs. Target (0.88)', 'FontSize', 14);
xlabel('RPM'); ylabel('TPS (%)'); zlabel('Lambda');
legend('', 'Target Lambda Plane', 'Location', 'northeast');
colorbar; view(3); hold off;

% --- FIGURE 3: ADVANCED SHIFT ANALYSIS ---
figure('Name', 'Shift Performance Analysis', 'NumberTitle', 'off', 'WindowState', 'maximized');
shiftIndices = find(diff(logData.Gear) > 0); % Find all upshifts
timeWindow = -0.5:0.02:0.5; % -500ms to +500ms around the shift
hold on;
for i = 1:length(shiftIndices)
    idx = shiftIndices(i);
    if idx > 25 && idx < (height(logData) - 25)
        timeAtShift = logData.("Time(ms)")(idx) / 1000;
        rpmTrace = logData.RPM(idx-25:idx+25);
        timeTrace = (logData.("Time(ms)")(idx-25:idx+25) / 1000) - timeAtShift;
        plot(timeTrace, rpmTrace, 'Color', [0.5 0.5 0.5 0.5]); % Plot individual shifts transparently
    end
end
xline(0, '--r', 'Shift Event', 'LineWidth', 2);
title('RPM Profile During Upshifts (All Shifts Overlaid)');
xlabel('Time Relative to Shift (s)'); ylabel('RPM');
grid on; hold off;

%% 6. Display Session Summary
fprintf('\n--- Session Summary ---\n');
fprintf('Total Logged Duration: %.2f minutes\n', time_s(end) / 60);
fprintf('Maximum RPM Reached: %d RPM\n', max(logData.RPM, [], 'omitnan'));
fprintf('Maximum Coolant Temp: %.1f °C\n', max(logData.("Coolant(C)"), [], 'omitnan'));
fprintf('Battery Voltage (Avg): %.2f V\n', mean(logData.("VBAT(V)"), 'omitnan'));
fprintf('Total Gear Shifts: %d\n', sum(abs(diff(logData.Gear(~isnan(logData.Gear)))) > 0));
fprintf('CAN Timeout Events: %d\n', sum(timeoutIndices));
fprintf('Data Integrity: %.2f%% VALID (%d corrupted data lines)\n', integrityPercentage, invalidCrcCount);
fprintf('\nAnalysis complete.\n');
