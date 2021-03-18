clear all;
set(0,'DefaultFigureWindowStyle','docked')

% Generate a chirp and write to file

fs = 14e6;%56e6; % samples/second % 40e6
dt = 1/fs;
f0 = 1e6; % Hz
f1 = 1e6;%25e6; % Hz
t1 = 10e-6;%500e-6; %10e-6; % seconds % 0.01

t_zero_before = 10e-6; % seconds - pad with zeros before
t_zero_after = 10e-6; % seconds - pad with zeros after

t_chrp = 0:dt:(t1-dt);
t = 0:dt:((t1+t_zero_before+t_zero_after)-dt);

y_chrp = 0.5*chirp(t_chrp,f0,t1,f1);
y_chrp = [zeros(1, t_zero_before*fs) complex(y_chrp) zeros(1, t_zero_after*fs)];
y = y_chrp;

samps = [real(y); imag(y)];
samps = samps(:)';

fileID = fopen('chirp.bin','w');
fwrite(fileID,samps,'float');

%% plot
figure
subplot(2,1,1)
plot(t, real(y), '-*')
subplot(2,1,2)
plot(t, abs(fftshift(fft(y))))
