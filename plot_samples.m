rx_length_multiplier = 3;
samples_saved = 10;

n_samps = length(y)*rx_length_multiplier*samples_saved;
t_total = n_samps / fs
dt = 1 / fs;
bytes_per_samp = 4;
t = t:dt:(t_total-dt);

f = fopen('rx_samps.bin');

r_part = fread(f, n_samps, 'float', bytes_per_samp);
fseek(f, bytes_per_samp, 'bof');
i_part = fread(f, n_samps, 'float', bytes_per_samp);
fclose(f);

z = complex(r_part, i_part);



%%
% y_compare = [y 0*y];
% figure
% subplot(n_plots,1,1)
% plot(t, real(y_compare))
% title('TX - Time')
% subplot(n_plots,1,2)
% plot(t, real(z))
% title('RX - Time')
% 
% subplot(n_plots,1,3)
% df = fs/n_samps; % Hz
% f = -fs/2:df:fs/2-df; % Hz
% plot(f, abs(fftshift(fft(z)))/n_samps)
% title('RX - FFT')
% ylim([0 5e-3])
% subplot(n_plots,1,4)
% plot(f, abs(fftshift(fft(y_compare)))/n_samps)
% title('TX - FFT')


%% Xcor

[acor, lag] = xcorr(z,y);

figure

subplot(3,1,1)
plot(1e6*(((1:length(acor))./fs) - t_total), abs(acor)/max(abs(acor)))
title('Normalized Cross-Correlation')
%xlim([-inf, 100])
xlabel('\muS')

subplot(3,1,2)
plot(1e6*(((1:length(acor))./fs) - t_total), log10(abs(acor)))
title('Log Cross Correlation')
%xlim([-inf, 100])
xlabel('\muS')

subplot(3,1,3)
plot(real(z))
title('Samples')

[~,I] = max(abs(acor));
lagDiff = lag(I)
lagTime = lagDiff / fs