rx_length_multiplier = 3;
samples_saved = 1000;

n_samps = length(y)*rx_length_multiplier*samples_saved;
t_total = n_samps / fs
dt = 1 / fs;
bytes_per_samp = 4;
t = t:dt:(t_total-dt);

f = fopen('rx_samps.bin');
%f = fopen('figures/20210329-stick-radar-driveway-data/rx_samps_walking_around.bin');

r_part = fread(f, n_samps, 'float', bytes_per_samp);
fseek(f, bytes_per_samp, 'bof');
i_part = fread(f, n_samps, 'float', bytes_per_samp);
fclose(f);

z = complex(r_part, i_part);

z_prestack = reshape(z, [length(y)*rx_length_multiplier, samples_saved]);

if false
    z_ref = z_prestack(:,2);l
    for idx = 1:samples_saved
        z_prestack(:,idx) = z_ref;
    end
end

% add noise
sigma = 0;
z_prestack = z_prestack + sigma*randn(size(z_prestack)) + j*sigma*randn(size(z_prestack));


%% sum and estimate

figure
sum_range = [1 100 499 999]; %1:50:999;
snrs = zeros(length(sum_range),1);

for idx = 1:length(sum_range)
    n_sums = sum_range(idx)

    z_sum = mean(z_prestack(:,2:n_sums+1),2); % coherent sum of each sample
    [acor, lag] = xcorr(z_sum,y);

    cor_power = 20*log10(abs(acor));

    plot(cor_power)
    xlim([2100 2300])
    ylim([-30, 25])
    xlabel('Lag [samples]')
    ylabel('Cross Correlation Power [dB]')
    title(sprintf('Cross Correlation Power with %d samples summed', n_sums))

    valid_range = cor_power(1500:3900);
    [~,I] = max(valid_range);
    noise_floor_samples = [valid_range(1:I-10); valid_range(I+10:end)];
    signal = valid_range(I);
    noise = mean(noise_floor_samples);
    snr = signal-noise
    snrs(idx) = signal - noise;
    drawnow
    pause(2)
end

%% Distribution of phase

plot_first_samples_n = 500

if plot_first_samples_n > 0
    figure
end

peak_xcor_lag = zeros(samples_saved,1);
peak_xcor_ang = zeros(samples_saved,1);

for idx = 1:samples_saved
    [acor, lag] = xcorr(z_prestack(:,idx),y);
    if idx < plot_first_samples_n
        plot(1e6*(((1:length(acor))./fs)), abs(acor))
        hold on
    end
    
    % find peak
    [~,I] = max(abs(acor));
    lagDiff = lag(I);
    pkMag = abs(acor(I));
    pkAng = angle(acor(I));
    [lagDiff pkMag pkAng];
    
    peak_xcor_lag(idx) = lagDiff;
    peak_xcor_ang(idx) = pkAng;
end
if plot_first_samples_n > 0
    xlabel('\muS')
end

figure
plot(peak_xcor_lag)
xlabel('Chirp index'); ylabel('Cross-correlation Peak [sample difference]')
figure
plot(peak_xcor_ang)
xlabel('Chirp index'); ylabel('Phase of cross-correlation peak');
figure
hist(peak_xcor_ang(2:end))
xlabel('Phase of xcorr peak [rad]'); ylabel('Chirp count')