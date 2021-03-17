
t_total = 10*t1;
n_samps = fs*t_total;
dt = 1 / fs;
bytes_per_samp = 4;
t = t:dt:(t_total-dt);

n_plots = 4;

f = fopen('rx_samps.bin');
%f = fopen('../../test_data/20180128_seq/test8.bin');
%f = fopen('chirp.bin')

r_part = fread(f, n_samps, 'float', bytes_per_samp);
fseek(f, bytes_per_samp, 'bof');
i_part = fread(f, n_samps, 'float', bytes_per_samp);
fclose(f);

z = complex(r_part, i_part);

y_compare = [y 0*y];

%%

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

[acor, lag] = xcorr(y,z);

%subplot(n_plots,1,5)
figure
subplot(3,1,1)
plot(1e6*(((1:length(acor))./fs) - t_total), abs(acor)/max(abs(acor)), 'LineWidth', 2)
xlim([-inf, 100])
xlabel('\muS')
subplot(3,1,2)
plot(1e6*(((1:length(acor))./fs) - t_total), log10(abs(acor)), 'LineWidth', 2)
title('Cross-Correlation')
xlim([-inf, 100])
xlabel('\muS')
%xlim([-5 -1])
%hold on
%ylim([0 10])
subplot(3,1,3)
plot(real(z))

set(gcf,'color','w');
set(gca,'FontSize', 24);

[~,I] = max(abs(acor));
lagDiff = lag(I)
lagTime = lagDiff / fs