DROP FUNCTION IF EXISTS checkrr;
DROP FUNCTION IF EXISTS checkname;
DROP FUNCTION IF EXISTS raise_error;

CREATE FUNCTION checkrr RETURNS STRING SONAME "datacheck.so";
CREATE FUNCTION checkname RETURNS STRING SONAME "datacheck.so";
CREATE FUNCTION raise_error RETURNS STRING SONAME "datacheck.so";


DROP TRIGGER IF EXISTS recordsBefIns;


DELIMITER $$$

CREATE TRIGGER recordsBefIns BEFORE INSERT ON records
FOR EACH ROW
BEGIN
    DECLARE dummy INT;

-- You won't like this; I do. Remove if you want but don't forget to
-- change the regexps for domain name

   SET NEW.name = LOWER(NEW.name);                              -- optional

   SET NEW.type = UPPER(NEW.type);                              -- enforce uppercase type

   IF NEW.type NOT IN ('A', 'AAAA', 'DS', 'NS', 'PTR', 'SOA', 'SRV', 'TXT') THEN
	SET @txt =  raise_error('Unsupported record type');
   END IF;


   SET @msg = checkname(NEW.type, NEW.name);
   IF SUBSTR(@msg, 1, 1) = 'N' THEN
	   SET @txt =  raise_error( CONCAT('Invalid record NAME: ', SUBSTR(@msg, 3)) );
   END IF;

   SET @msg = checkrr(NEW.type, NEW.content);
   IF SUBSTR(@msg, 1, 1) = 'N' THEN
	   SET @txt =  raise_error( CONCAT('Invalid record CONTENT: ', SUBSTR(@msg, 3)) );
   END IF;


END $$$

DELIMITER ;

