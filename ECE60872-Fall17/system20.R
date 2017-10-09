# System,machine type,nodes,procstot,procsinnode,nodenum,nodenumz,node install,node prod,node decom,fru type,mem,cputype,memtype,num intercon,purpose,Prob Started,Prob Fixed,Down Time,Facilities,Hardware,Human Error,Network,Undetermined,Software,Same Event
# 2,cluster,49,6152,80,0,0,5-Apr,5-Jun,current,part,80,1,1,0,graphics.fe,6/21/2005 10:54,6/21/2005 11:00,6,,Graphics Accel Hdwr,,,,,No

# Plot System 20's failure interval CDF
# Yizhou Shan (shan13@purdue.edu)

library(fitdistrplus)

get_system=function(sys.num)
{
	sys.n=fail.rec[fail.rec$System == sys.num, ]
	return(sys.n)
}

fail.rec=read.csv("/Users/lastweek/Downloads/LANL-fail/LA-UR-05-7318-failure-data-1996-2005.csv")

sys.20=get_system(20)

d1=as.Date(sys.20$Prob.Started, "%m/%d/%Y %H:%M")
d2=as.Date(sys.20$Prob.Started, "%m/%d/%Y %H:%M")
d1=sort(d1, decreasing = FALSE)
d2=sort(d2, decreasing = FALSE)

d2=data.table::shift(d2,1)
d3=difftime(d1,d2, units="mins")

# Maximum 44640 min
# Total 2378 failures
d3=sort(d3)

ecdf1=ecdf(d3)
ecdf2=ecdf(rexp(2378,0.001))
ecdf3=ecdf(rweibull(2378, shape=1, scale=1000))
ecdf4=ecdf(rlnorm(2378, log(900), log(2)))
n <- rnorm(2378, 900, 2000)
n <- n[n > 0]
ecdf5=ecdf(n)

plot(ecdf1, verticals=TRUE, do.points=FALSE, col="red", main="CDF of System 20 Failure Interval (mins)",
     	xlab="Time between failures (mins)", ylab="CDF", log='x',xlim=c(1, 50000), xaxt="n")
ticks <- seq(-2, 4, by=1)
labels <- sapply(ticks, function(i) as.expression(bquote(10^ .(i))))
axis(1, at=c(0.01, 0.1, 1, 10, 100,1000,10000), labels=labels)

lines(ecdf2, col="green")
lines(ecdf3, col="blue")
lines(ecdf4, col="yellow")
lines(ecdf5, col="black")

legend('right', legend=c("Data", "Expo", "Weibull", "Lognormal", "Normal"), 
          lty=1, col=c('red', 'green', 'blue', 'yellow', 'black'), bty='n', cex=.75)
