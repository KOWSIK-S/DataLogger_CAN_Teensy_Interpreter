%% MOTOTSTUDENT - ADVANCED SESSION ANALYSIS SUITE V3.4 (FINAL)
% This script automatically finds, stitches, verifies, and visualizes all
% log files from a complete session, providing advanced graphical analysis.
% FIX: Replaced name-based column access with index-based access for
%      maximum robustness against MATLAB's variable naming rules.

clear; clc; close all;

%% 1. Select Session Directory
fprintf('--- MotoStudent Data Analysis ---\n');
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

%% 2. Sort Log Files Correctly
fprintf('Found %d log files. Sorting them chronologically...\n', length(logFiles));
fileNumbers = zeros(1, length(logFiles));
for k = 1:length(logFiles)
    token = regexp(logFiles(k).name, 'LOG_(\d+)\.CSV', 'tokens');
    if ~isempty(token)
        fileNumbers(k) = str2double(token{1}{1});
    end
end
[~, sortIdx] = sort(fileNumbers);
logFiles = logFiles(sortIdx);

%% 3. Stitch Log Files with Consistent Import Options
masterLogData = table();
lastTimestamp = 0;
fprintf('Stitching and verifying files...\n');

% Define import options based on the first file to ensure consistency
firstFilePath = fullfile(folderPath, logFiles(1).name);
opts = detectImportOptions(firstFilePath);
opts = setvartype(opts, 'double'); % Treat all columns as numeric data
opts.VariableNamesLine = 1;
opts.VariableNamingRule = 'modify';

for i = 1:length(logFiles)
    currentFile = fullfile(folderPath, logFiles(i).name);
    fprintf('  -> Processing %s...\n', logFiles(i).name);
    
    try
        tempData = readtable(currentFile, opts);
    catch ME
        fprintf('     WARNING: Could not read file %s. Error: %s. Skipping.\n', logFiles(i).name, ME.message);
        continue;
    end
    
    % Correct timestamps for a continuous session timeline
    if ~isempty(tempData)
        % --- THE FIX: Access the first column by index (1) ---
        % This is robust and doesn't depend on the column name.
        if iscell(tempData.(1))
            tempData.(1) = str2double(tempData.(1));
        end
        
        tempData.(1) = tempData.(1) + lastTimestamp;
        lastTimestamp = tempData.(1)(end);
    end
    
    masterLogData = [masterLogData; tempData];
end

if isempty(masterLogData)
    error('No valid data could be loaded from the log files.');
end

fprintf('Session stitching complete. Total data points: %d\n', height(masterLogData));

%% 4. Data Cleaning and Plotting
logData = masterLogData;
% Assign standard names now that the table is fully assembled
logData.Properties.VariableNames = {'Time_ms', 'RPM', 'TPS', 'Coolant', 'MAP', 'VBAT', 'Gear', 'Lambda1', 'CRC32'};

timeoutIndices = logData.RPM == -1;
logData.RPM(timeoutIndices) = NaN;
logData.TPS(timeoutIndices) = NaN;
logData.Coolant(timeoutIndices) = NaN;
logData.MAP(timeoutIndices) = NaN;
logData.VBAT(timeoutIndices) = NaN;
logData.Gear(timeoutIndices) = NaN;
logData.Lambda1(timeoutIndices) = NaN;
time_s = logData.Time_ms / 1000;

% --- (Plotting and summary code is unchanged from previous version) ---

% --- FIGURE 1: PROFESSIONAL DASHBOARD VIEW ---
figure('Name', 'Session Dashboard', 'NumberTitle', 'off', 'WindowState', 'maximized');
t = tiledlayout(2, 2, 'TileSpacing', 'compact', 'Padding', 'compact');
title(t, 'Race Session Dashboard', 'FontSize', 16, 'FontWeight', 'bold');
ax1 = nexttile;
plot(ax1, time_s, logData.RPM, 'Color', [0.8500 0.3250 0.0980], 'LineWidth', 2);
title(ax1, 'Engine RPM'); ylabel(ax1, 'RPM'); grid on;
ax2 = nexttile;
plot(ax2, time_s, logData.TPS, 'Color', [0 0.4470 0.7410], 'LineWidth', 2);
hold on;
area(ax2, time_s, logData.TPS, 'FaceColor', [0 0.4470 0.7410], 'FaceAlpha', 0.2, 'EdgeColor', 'none');
title(ax2, 'Throttle Position'); ylabel(ax2, 'TPS (%)'); grid on;
ax3 = nexttile;
plot(ax3, time_s, logData.Coolant, 'Color', [0.9290 0.6940 0.1250], 'LineWidth', 2);
title(ax3, 'Coolant Temperature'); ylabel(ax3, 'Temp (°C)'); xlabel(ax3, 'Time (s)'); grid on;
ax4 = nexttile;
stairs(ax4, time_s, logData.Gear, 'k-', 'LineWidth', 2.5);
title(ax4, 'Gear Position'); ylabel(ax4, 'Gear'); xlabel(ax4, 'Time (s)'); ylim([-1 6]); grid on;

% --- FIGURE 2: DRIVETRAIN ANALYSIS ---
figure('Name', 'Drivetrain Analysis', 'NumberTitle', 'off', 'WindowState', 'maximized');
t2 = tiledlayout(1, 2);
title(t2, 'Drivetrain Performance', 'FontSize', 16, 'FontWeight', 'bold');
ax5 = nexttile;
gears = logData.Gear(logData.Gear > 0 & ~isnan(logData.Gear));
histogram(ax5, gears, 'Normalization', 'probability', 'FaceColor', '#4DBEEE');
title(ax5, 'Time Spent in Each Gear');
xlabel(ax5, 'Gear'); ylabel(ax5, 'Percentage of Time');
ax5.XTick = 1:max(gears);
ax5.YAxis.TickLabelFormat = '%.0f%%';
ax6 = nexttile;
gscatter(logData.RPM, logData.Gear, logData.Gear, [], '.', 10);
title(ax6, 'RPM Operating Range per Gear');
xlabel(ax6, 'Engine RPM'); ylabel(ax6, 'Gear'); grid on;
legend('Location', 'eastoutside');

% --- FIGURE 3: 3D FUEL MAP ANALYSIS ---
figure('Name', '3D Fuel Map', 'NumberTitle', 'off', 'WindowState', 'maximized');
t3 = tiledlayout(1,2);
title(t3, 'Lambda (Air-Fuel Ratio) Map', 'FontSize', 16, 'FontWeight', 'bold');
validFuelIndices = ~isnan(logData.RPM) & ~isnan(logData.TPS) & ~isnan(logData.Lambda1);
F = scatteredInterpolant(logData.RPM(validFuelIndices), logData.TPS(validFuelIndices), logData.Lambda1(validFuelIndices), 'linear', 'none');
[rpmGrid, tpsGrid] = meshgrid(linspace(min(logData.RPM), max(logData.RPM), 50), linspace(min(logData.TPS), max(logData.TPS), 50));
lambdaGrid = F(rpmGrid, tpsGrid);
ax7 = nexttile;
surf(ax7, rpmGrid, tpsGrid, lambdaGrid, 'EdgeColor', 'none', 'FaceAlpha', 0.8);
title(ax7, '3D Lambda Surface');
xlabel(ax7, 'RPM'); ylabel(ax7, 'TPS (%)'); zlabel(ax7, 'Lambda');
colorbar; view(3);
ax8 = nexttile;
contourf(ax8, rpmGrid, tpsGrid, lambdaGrid, 15);
title(ax8, '2D Lambda Contour Map');
xlabel(ax8, 'RPM'); ylabel(ax8, 'TPS (%)');
colorbar;

%% 5. Display Session Summary
fprintf('\n--- Session Summary ---\n');
fprintf('Total Logged Duration: %.2f minutes\n', time_s(end) / 60);
fprintf('Maximum RPM Reached: %d RPM\n', max(logData.RPM, [], 'omitnan'));
fprintf('Maximum Coolant Temp: %.1f °C\n', max(logData.Coolant, [], 'omitnan'));
fprintf('Battery Voltage (Avg): %.2f V\n', mean(logData.VBAT, 'omitnan'));
fprintf('Total Gear Shifts: %d\n', sum(abs(diff(logData.Gear(~isnan(logData.Gear)))) > 0));
fprintf('CAN Timeout Events: %d\n', sum(timeoutIndices));
fprintf('\nAnalysis complete.\n');

