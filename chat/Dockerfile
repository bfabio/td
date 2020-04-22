# Build
#######
FROM gcc:9 as build

WORKDIR /src

COPY . .

RUN make && strip chatd

# Final
#######
FROM debian:buster-slim

RUN apt-get update \
 && apt-get install -y libglib2.0-0 \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/chatd /usr/local/sbin/chatd

CMD ["/usr/local/sbin/chatd", "-v"]

EXPOSE 10000
