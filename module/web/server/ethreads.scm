;;; Web I/O: Non-blocking HTTP

;; Copyright (C) 2012 Free Software Foundation, Inc.

;; This library is free software; you can redistribute it and/or
;; modify it under the terms of the GNU Lesser General Public
;; License as published by the Free Software Foundation; either
;; version 3 of the License, or (at your option) any later version.
;;
;; This library is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; Lesser General Public License for more details.
;;
;; You should have received a copy of the GNU Lesser General Public
;; License along with this library; if not, write to the Free Software
;; Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
;; 02110-1301 USA

;;; Commentary:
;;;
;;; This is the non-blocking HTTP implementation of the (web server)
;;; interface.
;;;
;;; Code:

(define-module (web server ethreads)
  #:use-module ((srfi srfi-1) #:select (fold))
  #:use-module (srfi srfi-9)
  #:use-module (web http)
  #:use-module (web request)
  #:use-module (web response)
  #:use-module (web server)
  #:use-module (ice-9 binary-ports)
  #:use-module (ice-9 suspendable-ports)
  #:use-module (ice-9 match)
  #:use-module (ice-9 ethreads))

(define (set-nonblocking! port)
  (fcntl port F_SETFL (logior O_NONBLOCK (fcntl port F_GETFL)))
  (setvbuf port 'block 1024))

(define (make-default-socket family addr port)
  (let ((sock (socket PF_INET SOCK_STREAM 0)))
    (setsockopt sock SOL_SOCKET SO_REUSEADDR 1)
    (fcntl sock F_SETFD FD_CLOEXEC)
    (bind sock family addr port)
    (set-nonblocking! sock)
    sock))

(define-record-type <server>
  (make-server econtext have-request-prompt)
  server?
  (econtext server-econtext)
  (have-request-prompt server-have-request-prompt))

;; -> server
(define* (open-server #:key
                      (host #f)
                      (family AF_INET)
                      (addr (if host
                                (inet-pton family host)
                                INADDR_LOOPBACK))
                      (port 8080)
                      (socket (make-default-socket family addr port)))
  (install-suspendable-ports!)
  ;; We use a large backlog by default.  If the server is suddenly hit
  ;; with a number of connections on a small backlog, clients won't
  ;; receive confirmation for their SYN, leading them to retry --
  ;; probably successfully, but with a large latency.
  (listen socket 1024)
  (set-nonblocking! socket)
  (sigaction SIGPIPE SIG_IGN)
  (let* ((ctx (make-econtext))
         (server (make-server ctx (make-prompt-tag "have-request"))))
    (spawn (lambda () (socket-loop server socket)) ctx)
    server))

(define (bad-request msg . args)
  (throw 'bad-request msg args))

(define (keep-alive? response)
  (let ((v (response-version response)))
    (and (or (< (response-code response) 400)
             (= (response-code response) 404))
         (case (car v)
           ((1)
            (case (cdr v)
              ((1) (not (memq 'close (response-connection response))))
              ((0) (memq 'keep-alive (response-connection response)))))
           (else #f)))))

(define (client-loop client have-request)
  (with-throw-handler #t
    (lambda ()
      (let loop ()
        (cond
         ((eof-object? (lookahead-u8 client))
          (close-port client))
         (else
          (call-with-values
              (lambda ()
                (catch #t
                  (lambda ()
                    (let* ((request (read-request client))
                           (body (read-request-body request)))
                      (suspend
                       (lambda (ctx thread)
                         (have-request thread request body)))))
                  (lambda (key . args)
                    (display "While reading request:\n" (current-error-port))
                    (print-exception (current-error-port) #f key args)
                    (values (build-response #:version '(1 . 0) #:code 400
                                            #:headers '((content-length . 0)))
                            #vu8()))))
            (lambda (response body)
              (write-response response client)
              (when body
                (put-bytevector client body))
              (force-output client)
              (if (and (keep-alive? response)
                       (not (eof-object? (peek-char client))))
                  (loop)
                  (close-port client))))))))
    (lambda (k . args)
      (catch #t
        (lambda () (close-port client))
        (lambda (k . args)
          (display "While closing port:\n" (current-error-port))
          (print-exception (current-error-port) #f k args))))))

(define (socket-loop server socket)
  (define (have-request client-thread request body)
    (abort-to-prompt (server-have-request-prompt server)
                     client-thread request body))
  (let loop ()
    (match (accept socket)
      ((client . sockaddr)
       ;; From "HOP, A Fast Server for the Diffuse Web", Serrano.
       (setsockopt client SOL_SOCKET SO_SNDBUF (* 12 1024))
       (set-nonblocking! client)
       ;; Always disable Nagle's algorithm, as we handle buffering
       ;; ourselves.  Ignore exceptions if it's not a TCP port, or
       ;; TCP_NODELAY is not defined on this platform.
       (false-if-exception
        (setsockopt client IPPROTO_TCP TCP_NODELAY 0))
       (spawn (lambda () (client-loop client have-request)))
       (loop)))))

;; -> (client request body | #f #f #f)
(define (server-read server)
  (call-with-prompt
   (server-have-request-prompt server)
   (lambda ()
     (run (server-econtext server)))
   (lambda (k client request body)
     (values client request body))))

;; -> 0 values
(define (server-write server client response body)
  (resume client (lambda () (values response body)) (server-econtext server))
  (values))

;; -> unspecified values
(define (close-server server)
  (destroy-econtext (server-econtext server)))

(define-server-impl ethreads
  open-server
  server-read
  server-write
  close-server)
