.PHONY: clear
clear:
	find . -name "*:Zone.Identifier" -type f -delete
	find . -name "*.aux" -type f -delete
	find . -name "*.gz" -type f -delete
	find . -name "*.out" -type f -delete
	find . -name "*.txss2" -type f -delete
	find . -name "*.log" -type f -delete